#include "AlgoTradingClient.hpp"
#include "L2Book.hpp"
#include "SharedMarketData.hpp"
#include "DisplayUtil.hpp"
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <cmath>

namespace Exchange {

class Insider : public AlgoTradingClient {
public:
    Insider(const Config& config, uint32_t target_symbol) 
        : AlgoTradingClient(config), target_symbol_(target_symbol) {
        book_.symbol_id = target_symbol;
        connect_shm();
    }

    void on_l2_update(const L2Update* update) override {
        if (update->symbol_id() == target_symbol_) {
            book_.update(update->side(), update->p(), update->q());
        }
    }

    void on_l3_update(const L3Update*) override {}

    void on_order_response(const OrderResponse* response) override {
        AlgoTradingClient::on_order_response(response);
        if (response->exec_type() == ExecType_Fill || response->exec_type() == ExecType_PartialFill) {
            std::lock_guard<std::mutex> lock(strategy_mtx_);
            int64_t current_pos = account_.get_position(target_symbol_);
            // Update entry price when we enter a new position
            if ((current_pos > 0 && response->side() == Side_Buy) || 
                (current_pos < 0 && response->side() == Side_Sell)) {
                entry_price_ = response->p();
            }
        }
    }

    void on_timer() override {
        if (!is_ready() || !shm_ptr_) return;

        double curr, last;
        shm_ptr_->read_price(curr, last);

        // Insider observes 'curr' (the future/true price)
        double true_price = curr;

        std::lock_guard<std::mutex> lock(book_.mutex);
        std::lock_guard<std::mutex> strat_lock(strategy_mtx_);
        
        int64_t cash = account_.get_cash();
        int64_t pos = account_.get_position(target_symbol_);
        auto open_orders = account_.get_open_orders();

        // Count pending orders
        int64_t pending_buy_qty = 0;
        int64_t pending_sell_qty = 0;
        for (const auto& o : open_orders) {
            if (o.symbol_id != target_symbol_) continue;
            if (o.side == Side_Buy) pending_buy_qty += o.q;
            else if (o.side == Side_Sell) pending_sell_qty += o.q;
        }

        // Bracket Order Parameters
        double threshold = 2.0; 
        double take_profit_ticks = 5.0;
        double stop_loss_ticks = 3.0;

        if (pos == 0) {
            closing_ = false;
            // Clean up any lingering orders when flat
            if (!open_orders.empty()) {
                for (const auto& o : open_orders) {
                    cancel_order(o.order_id, target_symbol_, o.side);
                }
                return; 
            }

            // Strategy: If true_price is significantly higher than best ask, BUY.
            // If true_price is significantly lower than best bid, SELL.
            if (!book_.asks.empty()) {
                auto best_ask = book_.asks.begin();
                if (true_price > (double)best_ask->first + threshold) {
                    new_market_order(target_symbol_, Side_Buy, 5);
                    display_.add_message("Snipe BUY @ " + std::to_string(best_ask->first));
                    return; 
                }
            }

            if (!book_.bids.empty()) {
                auto best_bid = book_.bids.begin();
                if (true_price < (double)best_bid->first - threshold) {
                    new_market_order(target_symbol_, Side_Sell, 5);
                    display_.add_message("Snipe SELL @ " + std::to_string(best_bid->first));
                }
            }
        } else if (pos > 0) {
            // We are LONG
            if (pending_sell_qty == 0 && !closing_) {
                int64_t tp_price = static_cast<int64_t>(std::round(entry_price_ + take_profit_ticks));
                new_limit_order(target_symbol_, Side_Sell, tp_price, pos);
                display_.add_message("Bracket TP (SELL) @ " + std::to_string(tp_price));
            }

            if (true_price <= entry_price_ - stop_loss_ticks && !closing_) {
                closing_ = true;
                for (const auto& o : open_orders) {
                    if (o.side == Side_Sell) cancel_order(o.order_id, target_symbol_, Side_Sell);
                }
                new_market_order(target_symbol_, Side_Sell, pos);
                display_.add_message("Stop Loss Hit! Market SELL to close.");
            }
        } else if (pos < 0) {
            // We are SHORT
            if (pending_buy_qty == 0 && !closing_) {
                int64_t tp_price = static_cast<int64_t>(std::round(entry_price_ - take_profit_ticks));
                new_limit_order(target_symbol_, Side_Buy, tp_price, std::abs(pos));
                display_.add_message("Bracket TP (BUY) @ " + std::to_string(tp_price));
            }

            if (true_price >= entry_price_ + stop_loss_ticks && !closing_) {
                closing_ = true;
                for (const auto& o : open_orders) {
                    if (o.side == Side_Buy) cancel_order(o.order_id, target_symbol_, Side_Buy);
                }
                new_market_order(target_symbol_, Side_Buy, std::abs(pos));
                display_.add_message("Stop Loss Hit! Market BUY to close.");
            }
        }

        display_.display("Insider", config_.client_id, target_symbol_, pos, cash, true_price, account_.get_open_orders());
    }

private:
    void connect_shm() {
        int fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd == -1) return;
        shm_ptr_ = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
    }

    uint32_t target_symbol_;
    L2Book book_;
    SharedMarketData* shm_ptr_ = nullptr;
    DisplayFramework display_;

    std::mutex strategy_mtx_;
    double entry_price_ = 0.0;
    bool closing_ = false;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 301;
    config.symbol_ids = {1};
    config.timer_interval_ms = 100;

    Exchange::Insider insider(config, 1);
    return insider.run();
}

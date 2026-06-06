#include "AlgoTradingClient.hpp"
#include "SharedMarketData.hpp"
#include "DisplayUtil.hpp"
#include <iostream>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <cmath>
#include <random>

namespace Exchange {

class MartingaleTrader : public AlgoTradingClient {
public:
    MartingaleTrader(const Config& config, uint32_t target_symbol) 
        : AlgoTradingClient(config), target_symbol_(target_symbol) {
        connect_shm();
    }

    void on_l2_update(const L2Update*) override {}
    void on_l3_update(const L3Update*) override {}

    void on_order_response(const OrderResponse* response) override {
        AlgoTradingClient::on_order_response(response);
        if (response->exec_type() == ExecType_Fill || response->exec_type() == ExecType_PartialFill) {
            std::lock_guard<std::mutex> lock(strategy_mtx_);
            int64_t current_pos = account_.get_position(target_symbol_);
            
            // If we just opened/increased a position, track the entry price
            if ((current_pos > 0 && response->side() == Side_Buy) || 
                (current_pos < 0 && response->side() == Side_Sell)) {
                entry_price_ = response->p();
            }
            
            // If we just closed a position, check if it was a win or loss
            if (current_pos == 0) {
                double exit_price = response->p();
                bool was_long = (response->side() == Side_Sell); // Sell to close a long
                
                bool win = false;
                if (was_long) {
                    win = (exit_price > entry_price_);
                } else {
                    win = (exit_price < entry_price_);
                }

                if (win) {
                    multiplier_ = 1;
                    display_.add_message("WIN! Resetting multiplier to 1.");
                } else {
                    multiplier_ = std::min(multiplier_ * 2, max_multiplier_);
                    display_.add_message("LOSS. Doubling multiplier to " + std::to_string(multiplier_) + ".");
                }
            }
        }
    }

    void on_timer() override {
        if (!is_ready() || !shm_ptr_) return;

        double curr, last;
        shm_ptr_->read_price(curr, last);
        double price = last;

        std::lock_guard<std::mutex> lock(strategy_mtx_);
        int64_t cash = account_.get_cash();
        int64_t pos = account_.get_position(target_symbol_);
        auto open_orders = account_.get_open_orders();

        int64_t pending_buy_qty = 0;
        int64_t pending_sell_qty = 0;
        for (const auto& o : open_orders) {
            if (o.symbol_id != target_symbol_) continue;
            if (o.side == Side_Buy) pending_buy_qty += o.q;
            else if (o.side == Side_Sell) pending_sell_qty += o.q;
        }

        double tp_ticks = 5.0;
        double sl_ticks = 5.0;

        if (pos == 0) {
            closing_ = false;
            // Clean up ghost orders
            if (!open_orders.empty()) {
                for (const auto& o : open_orders) cancel_order(o.order_id, target_symbol_, o.side);
                return;
            }

            // Decide direction (Martingale often just picks one or flips)
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> side_dist(0, 1);
            Side side = (side_dist(gen) == 0) ? Side_Buy : Side_Sell;

            uint64_t qty = base_qty_ * multiplier_;
            new_market_order(target_symbol_, side, qty);
            display_.add_message("Entry: " + std::string(side == Side_Buy ? "BUY" : "SELL") + " qty=" + std::to_string(qty));
            
        } else if (pos > 0) {
            // LONG
            if (pending_sell_qty == 0 && !closing_) {
                int64_t tp_price = static_cast<int64_t>(std::round(entry_price_ + tp_ticks));
                new_limit_order(target_symbol_, Side_Sell, tp_price, pos);
                display_.add_message("Martingale TP (SELL) @ " + std::to_string(tp_price));
            }
            if (price > 0 && price <= entry_price_ - sl_ticks && !closing_) {
                closing_ = true;
                for (const auto& o : open_orders) if (o.side == Side_Sell) cancel_order(o.order_id, target_symbol_, Side_Sell);
                new_market_order(target_symbol_, Side_Sell, pos);
                display_.add_message("Martingale SL: Market SELL");
            }
        } else if (pos < 0) {
            // SHORT
            if (pending_buy_qty == 0 && !closing_) {
                int64_t tp_price = static_cast<int64_t>(std::round(entry_price_ - tp_ticks));
                new_limit_order(target_symbol_, Side_Buy, tp_price, std::abs(pos));
                display_.add_message("Martingale TP (BUY) @ " + std::to_string(tp_price));
            }
            if (price > 0 && price >= entry_price_ + sl_ticks && !closing_) {
                closing_ = true;
                for (const auto& o : open_orders) if (o.side == Side_Buy) cancel_order(o.order_id, target_symbol_, Side_Buy);
                new_market_order(target_symbol_, Side_Buy, std::abs(pos));
                display_.add_message("Martingale SL: Market BUY");
            }
        }

        display_.display("Martingale", config_.client_id, target_symbol_, pos, cash, price, account_.get_open_orders());
    }

private:
    void connect_shm() {
        int fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd == -1) return;
        shm_ptr_ = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
    }

    uint32_t target_symbol_;
    SharedMarketData* shm_ptr_ = nullptr;
    DisplayFramework display_;

    std::mutex strategy_mtx_;
    double entry_price_ = 0.0;
    bool closing_ = false;

    uint64_t base_qty_ = 2;
    uint64_t multiplier_ = 1;
    uint64_t max_multiplier_ = 32;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 601;
    config.symbol_ids = {1};
    config.timer_interval_ms = 1000;

    Exchange::MartingaleTrader strategy(config, 1);
    return strategy.run();
}

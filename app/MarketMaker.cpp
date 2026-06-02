#include "AlgoTradingClient.hpp"
#include "ClientAccount.hpp"
#include "L2Book.hpp"
#include "LogUtil.hpp"
#include "SharedMarketData.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <cmath>
#include <atomic>
#include <algorithm>
#include <deque>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define LAYER 5

namespace Exchange {

// --- Advanced Market Maker ---
class MarketMaker : public AlgoTradingClient {
public:
    MarketMaker(const Config& config, uint32_t target_symbol) 
        : AlgoTradingClient(config), target_symbol_(target_symbol) {
        book_.symbol_id = target_symbol;
        last_update_time_ = std::chrono::steady_clock::now();
        connect_shm();
    }

    ~MarketMaker() {
        if (shm_ptr_) munmap(shm_ptr_, sizeof(SharedMarketData));
    }

    void on_l2_update(const L2Update* update) override {
        if (update->symbol_id() == target_symbol_) {
            book_.update(update->side(), update->p(), update->q());
        }
    }

    void on_l3_update(const L3Update* /*update*/) override {}

    void on_order_response(const OrderResponse* response) override {
        account_.handle_order_response(response);
        if (response->exec_type() == ExecType_PartialFill || response->exec_type() == ExecType_Fill) {
            std::lock_guard<std::mutex> lock(strategy_mtx_);
            double impact = static_cast<double>(response->q()) * toxicity_impact_factor_;
            if (response->side() == Side_Buy) {
                toxicity_skew_ -= impact;
            } else if (response->side() == Side_Sell) {
                toxicity_skew_ += impact;
            }
        }
    }

    void on_position_response(const PositionResponse* response) override {
        account_.handle_position_response(response);
    }

    void run_strategy() {
        if (!is_ready() || !shm_ptr_) return;

        double curr, last;
        shm_ptr_->read_price(curr, last);
        
        // Market Maker observes 'last' (stale) price
        double observed_price = last;

        std::lock_guard<std::mutex> lock(strategy_mtx_);
        
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last_update_time_;
        last_update_time_ = now;
        
        double decay_rate = std::log(2.0) / 2.0; 
        toxicity_skew_ *= std::exp(-decay_rate * elapsed.count());

        fair_price_ = observed_price;
        
        price_history_.push_back(fair_price_);
        if (price_history_.size() > 20) {
            price_history_.pop_front();
        }
        
        double variance = 0.0;
        if (price_history_.size() > 1) {
            double mean = 0.0;
            for (double p : price_history_) mean += p;
            mean /= price_history_.size();
            for (double p : price_history_) variance += (p - mean) * (p - mean);
            variance /= (price_history_.size() - 1);
        }
        double stddev = std::sqrt(variance);

        double base_spread = 2.0; 
        double dynamic_spread = base_spread + stddev * volatility_factor_;
        const uint64_t order_qty = 5;
        const int64_t price_tolerance = 1;

        int64_t pos = account_.get_position(target_symbol_);
        int64_t cash = account_.get_cash();
        
        double pos_value = static_cast<double>(pos) * fair_price_;
        double equity = static_cast<double>(cash) + pos_value;
        
        double asset_ratio = 0.0;
        if (equity > 0.0) {
            asset_ratio = pos_value / equity; 
        }

        double inventory_skew = -asset_ratio * inventory_risk_factor_;
        
        double extreme_risk_spread_widener = 0.0;
        if (std::abs(asset_ratio) > 0.3) {
            extreme_risk_spread_widener = (std::abs(asset_ratio) - 0.3) * 15.0;
        }

        double total_skew = toxicity_skew_ + inventory_skew;
        total_skew = std::max(-50.0, std::min(50.0, total_skew));

        std::vector<int64_t> target_bids(LAYER);
        std::vector<int64_t> target_asks(LAYER);

        double adjusted_mid = fair_price_ + total_skew;
        
        for (int i = 0; i < LAYER; ++i) {
            double layer_spread = dynamic_spread + i * 1.5 + extreme_risk_spread_widener;
            target_bids[i] = static_cast<int64_t>(std::round(adjusted_mid - layer_spread));
            target_asks[i] = static_cast<int64_t>(std::round(adjusted_mid + layer_spread));
            if (target_bids[i] >= target_asks[i]) {
                target_bids[i] = static_cast<int64_t>(std::floor(adjusted_mid)) - 1;
                target_asks[i] = static_cast<int64_t>(std::ceil(adjusted_mid)) + 1;
            }
        }

        auto open_orders = account_.get_open_orders();
        std::vector<uint64_t> bid_order_ids;
        std::vector<uint64_t> ask_order_ids;

        for (const auto& order : open_orders) {
            if (order.symbol_id != target_symbol_) continue;
            if (order.side == Side_Buy) bid_order_ids.push_back(order.order_id);
            else if (order.side == Side_Sell) ask_order_ids.push_back(order.order_id);
        }

        auto get_order = [&](uint64_t id) {
            for (const auto& o : open_orders) if (o.order_id == id) return &o;
            return (const OrderResponseT*)nullptr;
        };

        std::sort(bid_order_ids.begin(), bid_order_ids.end(), [&](uint64_t a, uint64_t b) {
            auto oa = get_order(a); auto ob = get_order(b);
            if (!oa || !ob) return false;
            return oa->p > ob->p;
        });
        std::sort(ask_order_ids.begin(), ask_order_ids.end(), [&](uint64_t a, uint64_t b) {
            auto oa = get_order(a); auto ob = get_order(b);
            if (!oa || !ob) return false;
            return oa->p < ob->p;
        });

        for (int i = 0; i < LAYER; ++i) {
            if (i < (int)bid_order_ids.size()) {
                uint64_t oid = bid_order_ids[i];
                auto o = get_order(oid);
                if (o && std::abs(o->p - target_bids[i]) > price_tolerance) {
                    replace_order(oid, target_bids[i], order_qty, target_symbol_, Side_Buy);
                }
            } else {
                new_limit_order(target_symbol_, Side_Buy, target_bids[i], order_qty);
            }
        }
        for (size_t i = LAYER; i < bid_order_ids.size(); ++i) cancel_order(bid_order_ids[i], target_symbol_, Side_Buy);

        for (int i = 0; i < LAYER; ++i) {
            if (i < (int)ask_order_ids.size()) {
                uint64_t oid = ask_order_ids[i];
                auto o = get_order(oid);
                if (o && std::abs(o->p - target_asks[i]) > price_tolerance) {
                    replace_order(oid, target_asks[i], order_qty, target_symbol_, Side_Sell);
                }
            } else {
                new_limit_order(target_symbol_, Side_Sell, target_asks[i], order_qty);
            }
        }
        for (size_t i = LAYER; i < ask_order_ids.size(); ++i) cancel_order(ask_order_ids[i], target_symbol_, Side_Sell);

        display_status(total_skew, inventory_skew, toxicity_skew_, stddev, asset_ratio);
    }

private:
    void connect_shm() {
        int fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (fd == -1) {
            perror("shm_open");
            return;
        }
        shm_ptr_ = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (shm_ptr_ == MAP_FAILED) {
            perror("mmap");
            shm_ptr_ = nullptr;
        }
        close(fd);
    }

    void display_status(double total_skew, double inv_skew, double tox_skew, double vol, double asset_ratio) {
        static int count = 0;
        if (++count % 5 != 0) return; 

        std::cout << "\033[H\033[J";
        std::cout << "========== Advanced MM (Stale Price Observer) ==========" << std::endl;
        std::cout << "Target Symbol: " << target_symbol_ << " | Client ID: " << config_.client_id << std::endl;
        std::cout << "Fair Price (STALE): " << std::fixed << std::setprecision(2) << fair_price_ << std::endl;
        std::cout << "Position:           " << account_.get_position(target_symbol_) << std::endl;
        std::cout << "Cash:               " << account_.get_cash() << std::endl;
        std::cout << "Asset Ratio:        " << std::fixed << std::setprecision(4) << asset_ratio << std::endl;
        std::cout << "Total Skew:         " << std::fixed << std::setprecision(2) << total_skew << " (Inv: " << inv_skew << ", Tox: " << tox_skew << ")" << std::endl;
        
        std::cout << "\n[Open Orders]" << std::endl;
        auto orders = account_.get_open_orders();
        std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
            if (a.side != b.side) return a.side == Side_Sell;
            return (a.side == Side_Sell) ? a.p > b.p : a.p > b.p;
        });

        for (const auto& o : orders) {
            if (o.symbol_id != target_symbol_) continue;
            std::cout << "  " << (o.side == Side_Buy ? "BUY " : "SELL") << " " << std::setw(6) << o.p << " @ " << std::setw(4) << o.q << std::endl;
        }
    }

    uint32_t target_symbol_;
    L2Book book_;
    ClientAccount account_;
    SharedMarketData* shm_ptr_ = nullptr;
    
    std::mutex strategy_mtx_;
    double fair_price_ = 0.0;
    std::deque<double> price_history_;
    double toxicity_skew_ = 0.0;
    std::chrono::time_point<std::chrono::steady_clock> last_update_time_;
    
    const double toxicity_impact_factor_ = 0.5;
    const double volatility_factor_ = 1.0;
    const double inventory_risk_factor_ = 20.0;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 201;
    config.symbol_ids = {1};

    Exchange::MarketMaker mm(config, 1);
    
    std::thread strategy_thread([&mm]() {
        while (true) {
            mm.run_strategy();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    return mm.run();
}

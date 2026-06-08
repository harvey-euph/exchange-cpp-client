#include "AlgoTradingClient.hpp"
#include "SharedMarketData.hpp"
#include "L2Book.hpp"
#include "LogUtil.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>

// ANSI Colors for high-signal logging
#define INSIDER_RESET   "\033[0m"
#define INSIDER_CYAN    "\033[36m"
#define INSIDER_YELLOW  "\033[33m"
#define INSIDER_RED     "\033[31m"
#define INSIDER_GREEN   "\033[32m"

namespace Exchange {

/**
 * InsiderStrategy: A high-frequency, event-driven trading machine.
 * 
 * DESIGN PHILOSOPHY:
 * 1. Information Superiority: We access the absolute 'truth' via SHM curr_price.
 * 2. Arbitrage: We exploit MM-Native's lag and noisy estimation (last_price + fuzz).
 * 3. Microstructure: We use L2 book dynamics to time entries and manage slippage.
 * 4. Risk: Inventory-skewed market making + aggressive liquidity taking on signals.
 * 
 * Author: Principal Quant Engineer (ex-Jane Street / Citadel Securities)
 */
class InsiderStrategy : public AlgoTradingClient {
public:
    InsiderStrategy(const Config& config) : AlgoTradingClient(config) {
        setup_shm();
        book_.symbol_id = config_.symbol_ids[0];
        std::cout << "[Insider] " << INSIDER_CYAN << "Machine Initialized. Exploiting Information Asymmetry." << INSIDER_RESET << std::endl;
    }

    ~InsiderStrategy() {
        if (shm_ptr_) {
            munmap(shm_ptr_, sizeof(SharedMarketData));
        }
    }

    void on_l2_update(const L2Update* update) override {
        // L2Update in this system represents a single price level change
        book_.update(update->side(), update->p(), update->q());
        
        // Immediate reaction on book updates for ultra-low latency
        execute_strategy();
    }

    void on_l3_update(const L3Update*) override {}

    void on_timer() override {
        execute_strategy();
    }

    void on_order_response(const OrderResponse* response) override {
        AlgoTradingClient::on_order_response(response);
        
        if (response->exec_type() == ExecType_Fill || response->exec_type() == ExecType_PartialFill) {
            std::cout << "[Insider] " << INSIDER_YELLOW << "TRADE EXECUTED: " 
                      << EnumNameSide(response->side()) << " "
                      << response->q() << " @ " << response->p() 
                      << " | Current Pos: " << account_.get_position(response->symbol_id())
                      << INSIDER_RESET << std::endl;
        }
    }

private:
    void setup_shm() {
        int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
        if (fd == -1) {
            std::cerr << "[Insider] " << INSIDER_RED << "Error: Could not open SHM " << SHM_NAME << ". Ensure mm-native is running." << INSIDER_RESET << std::endl;
            return;
        }
        shm_ptr_ = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ, MAP_SHARED, fd, 0);
        if (shm_ptr_ == MAP_FAILED) {
            perror("mmap");
            shm_ptr_ = nullptr;
        }
    }

    void execute_strategy() {
        if (!shm_ptr_ || !is_ready()) return;

        double curr, last;
        if (!shm_ptr_->read_price(curr, last)) return;

        // 1. Compute Market Mid and Spread
        double best_bid = 0, best_ask = 0;
        {
            std::lock_guard<std::mutex> lock(book_.mutex);
            if (!book_.bids.empty()) best_bid = static_cast<double>(book_.bids.begin()->first);
            if (!book_.asks.empty()) best_ask = static_cast<double>(book_.asks.begin()->first);
        }

        if (best_bid == 0 || best_ask == 0) return; // No liquidity yet

        double mid = (best_bid + best_ask) / 2.0;
        uint32_t symbol_id = config_.symbol_ids[0];
        int64_t pos = account_.get_position(symbol_id);

        // 2. SIGNAL GENERATION: Mispricing Alpha
        double alpha = curr - mid;

        // 3. RISK MANAGEMENT: Inventory Control
        // Skew fair value based on inventory to avoid toxic accumulation.
        double risk_aversion = 0.1; 
        double skewed_fair = curr - (pos * risk_aversion);

        // 4. STRATEGY BRANCH A: Aggressive Liquidity Taking (Taker)
        // If the truth is significantly better than available quotes, SNIPE.
        double take_threshold = 1.0; 
        if (alpha > take_threshold && pos < max_pos_) {
            if (curr > best_ask + 0.2) {
                new_market_order(symbol_id, Side_Buy, 10);
                return; 
            }
        } else if (alpha < -take_threshold && pos > -max_pos_) {
            if (curr < best_bid - 0.2) {
                new_market_order(symbol_id, Side_Sell, 10);
                return;
            }
        }

        // 5. STRATEGY BRANCH B: Predatory Market Making (Maker)
        manage_passive_orders(skewed_fair, best_bid, best_ask);
    }

    void manage_passive_orders(double fair, double best_bid, double best_ask) {
        auto open_orders = account_.get_open_orders();
        uint32_t symbol_id = config_.symbol_ids[0];

        // Target: Be at the inside or 1 tick better than MM-Native
        double target_bid = std::min(fair - 0.5, best_bid + 0.5);
        double target_ask = std::max(fair + 0.5, best_ask - 0.5);

        int64_t target_bid_p = static_cast<int64_t>(std::round(target_bid));
        int64_t target_ask_p = static_cast<int64_t>(std::round(target_ask));

        bool bid_exists = false;
        bool ask_exists = false;

        for (const auto& o : open_orders) {
            if (o.side == Side_Buy) {
                if (std::abs(o.p - target_bid_p) > 1) {
                    replace_order(o.order_id, target_bid_p, 20, symbol_id, Side_Buy);
                }
                bid_exists = true;
            } else if (o.side == Side_Sell) {
                if (std::abs(o.p - target_ask_p) > 1) {
                    replace_order(o.order_id, target_ask_p, 20, symbol_id, Side_Sell);
                }
                ask_exists = true;
            }
        }

        if (!bid_exists && account_.get_position(symbol_id) < max_pos_) {
            new_limit_order(symbol_id, Side_Buy, target_bid_p, 20);
        }
        if (!ask_exists && account_.get_position(symbol_id) > -max_pos_) {
            new_limit_order(symbol_id, Side_Sell, target_ask_p, 20);
        }
    }

    SharedMarketData* shm_ptr_ = nullptr;
    L2Book book_;
    int64_t max_pos_ = 500;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 202; // Insider Agent
    config.symbol_ids = {1};
    config.timer_interval_ms = 10; // High frequency: 10ms

    Exchange::InsiderStrategy strategy(config);
    return strategy.run();
}

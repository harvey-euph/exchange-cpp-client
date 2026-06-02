#include "AlgoTradingClient.hpp"
#include "L2Book.hpp"
#include "SharedMarketData.hpp"
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
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

    void run_strategy() {
        if (!is_ready() || !shm_ptr_) return;

        double curr, last;
        shm_ptr_->read_price(curr, last);

        // Insider observes 'curr' (the future/true price)
        double true_price = curr;

        std::lock_guard<std::mutex> lock(book_.mutex);
        
        // Strategy: If true_price is significantly higher than best ask, BUY.
        // If true_price is significantly lower than best bid, SELL.
        double threshold = 2.0; 

        if (!book_.asks.empty()) {
            auto best_ask = book_.asks.begin();
            if (true_price > (double)best_ask->first + threshold) {
                std::cout << "[Insider] Snipe BUY! True Price: " << true_price << " Best Ask: " << best_ask->first << std::endl;
                new_market_order(target_symbol_, Side_Buy, 5);
            }
        }

        if (!book_.bids.empty()) {
            auto best_bid = book_.bids.begin();
            if (true_price < (double)best_bid->first - threshold) {
                std::cout << "[Insider] Snipe SELL! True Price: " << true_price << " Best Bid: " << best_bid->first << std::endl;
                new_market_order(target_symbol_, Side_Sell, 5);
            }
        }
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
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 301;
    config.symbol_ids = {1};

    Exchange::Insider insider(config, 1);
    
    std::thread strategy_thread([&insider]() {
        while (true) {
            insider.run_strategy();
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Insider polls faster
        }
    });

    return insider.run();
}

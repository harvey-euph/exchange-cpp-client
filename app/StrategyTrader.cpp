#include "AlgoTradingClient.hpp"
#include "SharedMarketData.hpp"
#include <iostream>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

namespace Exchange {

class StrategyTrader : public AlgoTradingClient {
public:
    StrategyTrader(const Config& config, uint32_t target_symbol) 
        : AlgoTradingClient(config), target_symbol_(target_symbol) {
        connect_shm();
    }

    void on_l2_update(const L2Update*) override {}
    void on_l3_update(const L3Update*) override {}
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

    void run_strategy() {
        if (!is_ready() || !shm_ptr_) return;

        double curr, last;
        shm_ptr_->read_price(curr, last);

        // Trend follower observes 'last' (published) price
        double observed_price = last;

        if (observed_price > last_price_ && last_price_ > 0) {
            consecutive_up_++;
            consecutive_down_ = 0;
            if (consecutive_up_ >= 3) {
                uint64_t qty = consecutive_up_ * 2;
                std::cout << "[Strategy] Trend UP! consecutive=" << consecutive_up_ << " BUY qty=" << qty << std::endl;
                new_market_order(target_symbol_, Side_Buy, qty);
            }
        } else if (observed_price < last_price_ && last_price_ > 0) {
            consecutive_down_++;
            consecutive_up_ = 0;
            if (consecutive_down_ >= 3) {
                uint64_t qty = consecutive_down_ * 2;
                std::cout << "[Strategy] Trend DOWN! consecutive=" << consecutive_down_ << " SELL qty=" << qty << std::endl;
                new_market_order(target_symbol_, Side_Sell, qty);
            }
        } else if (observed_price == last_price_) {
            // No change
        } else {
            // First price
        }

        last_price_ = observed_price;
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
    double last_price_ = 0.0;
    int consecutive_up_ = 0;
    int consecutive_down_ = 0;
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 501;
    config.symbol_ids = {1};

    Exchange::StrategyTrader strategy(config, 1);
    
    std::thread strategy_thread([&strategy]() {
        while (true) {
            strategy.run_strategy();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    return strategy.run();
}

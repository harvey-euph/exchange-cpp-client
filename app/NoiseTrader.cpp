#include "AlgoTradingClient.hpp"
#include "L2Book.hpp"
#include "SharedMarketData.hpp"
#include <iostream>
#include <random>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace Exchange {

class NoiseTrader : public AlgoTradingClient {
public:
    NoiseTrader(const Config& config, uint32_t target_symbol) 
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
        if (!is_ready()) return;

        std::lock_guard<std::mutex> lock(book_.mutex);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> side_dist(0, 1);
        
        if (side_dist(gen) == 0) { // BUY
            if (!book_.asks.empty()) {
                auto best_ask = book_.asks.begin();
                std::cout << "[Noise] Placing aggressive BUY at " << best_ask->first << std::endl;
                new_limit_order(target_symbol_, Side_Buy, best_ask->first, 2);
            }
        } else { // SELL
            if (!book_.bids.empty()) {
                auto best_bid = book_.bids.begin();
                std::cout << "[Noise] Placing aggressive SELL at " << best_bid->first << std::endl;
                new_limit_order(target_symbol_, Side_Sell, best_bid->first, 2);
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
    config.client_id = 401;
    config.symbol_ids = {1};

    Exchange::NoiseTrader noise(config, 1);
    
    std::thread strategy_thread([&noise]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> sleep_dist(500, 2000);

        while (true) {
            noise.run_strategy();
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
        }
    });

    return noise.run();
}

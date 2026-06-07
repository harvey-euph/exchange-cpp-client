#include "AlgoTradingClient.hpp"
#include "SharedMarketData.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <chrono>
#include <thread>
#include <csignal>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>

namespace Exchange {

class MarketMakerNative : public AlgoTradingClient {
public:
    MarketMakerNative(const Config& config) : AlgoTradingClient(config) {
        setup_shm();
        std::cout << "[MM-Native] Started. ClientID=" << config_.client_id << std::endl;
    }

    ~MarketMakerNative() {
        if (shm_ptr_) {
            shm_ptr_->running = false;
            munmap(shm_ptr_, sizeof(SharedMarketData));
        }
        shm_unlink(SHM_NAME);
    }

    void on_l2_update(const L2Update*) override {}
    void on_l3_update(const L3Update*) override {}

    void on_timer() override {
        if (!shm_ptr_) return;

        // 1. Update Price in SHM (Random Walk)
        double current_price = shm_ptr_->curr_price;
        current_price += dist_price_walk_(gen_);
        
        // Clamp price between 4000 and 6000
        if (current_price < 4000.0) current_price = 4000.0;
        else if (current_price > 6000.0) current_price = 6000.0;

        shm_ptr_->update_price(current_price);

        // 2. Market Simulation Logic
        double last_price = shm_ptr_->last_price;
        double estimation = last_price + dist_estimation_(gen_);

        // 3. Dynamic Order Management
        manage_orders(estimation);

        static int count = 0;
        if (++count % 10 == 0) {
            std::cout << "[MM-Native] SHM Price: " << std::fixed << std::setprecision(2) << current_price 
                      << " | Est: " << estimation << " | Active Orders: " << account_.get_open_orders().size() << std::endl;
        }
    }

private:
    void setup_shm() {
        // Try to unlink first in case it was left over
        shm_unlink(SHM_NAME);

        int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (fd == -1) {
            perror("shm_open");
            return;
        }
        if (ftruncate(fd, sizeof(SharedMarketData)) == -1) {
            perror("ftruncate");
            return;
        }
        shm_ptr_ = (SharedMarketData*)mmap(NULL, sizeof(SharedMarketData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (shm_ptr_ == MAP_FAILED) {
            perror("mmap");
            shm_ptr_ = nullptr;
            return;
        }
        
        // Initial state
        shm_ptr_->sequence = 0;
        shm_ptr_->curr_price = 5000.0;
        shm_ptr_->last_price = 5000.0;
        shm_ptr_->running = true;
        
        std::cout << "[MM-Native] SHM setup successful at " << SHM_NAME << std::endl;
    }

    void manage_orders(double estimation) {
        auto open_orders = account_.get_open_orders();
        
        int bids = 0, asks = 0;
        std::uniform_real_distribution<> flicker_dist(0.0, 1.0);
        
        // 1. Handle existing orders: Retreat, Cancel, or Flicker
        for (const auto& o : open_orders) {
            double p = static_cast<double>(o.p);
            bool should_flicker = flicker_dist(gen_) < 0.15; // 15% chance to replace anyway

            if (o.side == Side_Buy) {
                if (p > estimation - 1.0 || should_flicker) {
                    double new_p = estimation - 1.5 - std::abs(dist_noise_(gen_));
                    replace_order(o.order_id, static_cast<int64_t>(std::round(new_p)), o.q, o.symbol_id, o.side);
                    bids++;
                } else if (p < estimation - 60.0) { // Keep some depth
                    cancel_order(o.order_id, o.symbol_id, o.side);
                } else {
                    bids++;
                }
            } else if (o.side == Side_Sell) {
                if (p < estimation + 1.0 || should_flicker) {
                    double new_p = estimation + 1.5 + std::abs(dist_noise_(gen_));
                    replace_order(o.order_id, static_cast<int64_t>(std::round(new_p)), o.q, o.symbol_id, o.side);
                    asks++;
                } else if (p > estimation + 60.0) {
                    cancel_order(o.order_id, o.symbol_id, o.side);
                } else {
                    asks++;
                }
            }
        }

        // 2. Replenish Liquidity: Maintain at least 12 orders on each side
        int target_per_side = 12;
        for (int i = bids; i < target_per_side; ++i) {
            double p = estimation - 1.5 - std::abs(dist_noise_(gen_));
            uint64_t q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
            new_limit_order(1, Side_Buy, static_cast<int64_t>(std::round(p)), q);
        }
        for (int i = asks; i < target_per_side; ++i) {
            double p = estimation + 1.5 + std::abs(dist_noise_(gen_));
            uint64_t q = std::uniform_int_distribution<uint64_t>(5, 50)(gen_);
            new_limit_order(1, Side_Sell, static_cast<int64_t>(std::round(p)), q);
        }
    }

    SharedMarketData* shm_ptr_ = nullptr;
    std::mt19937 gen_{std::random_device{}()};
    std::normal_distribution<> dist_price_walk_{0, 1.5};
    std::normal_distribution<> dist_estimation_{0, 3.0};
    std::normal_distribution<> dist_noise_{0, 5.0};
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 100; // Native Market Maker
    config.symbol_ids = {1};
    config.timer_interval_ms = 100; // Faster tick (100ms)

    Exchange::MarketMakerNative mm(config);
    return mm.run();
}

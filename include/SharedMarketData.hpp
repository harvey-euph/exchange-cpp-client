#pragma once
#include <cstdint>
#include <atomic>

namespace Exchange {

struct SharedMarketData {
    std::atomic<uint64_t> sequence{0};
    double curr_price = 0.0;
    double last_price = 0.0;
    std::atomic<bool> running{false};

    // Helper to perform atomic updates
    void update_price(double new_price) {
        uint64_t seq = sequence.load(std::memory_order_relaxed);
        sequence.store(seq + 1, std::memory_order_release);
        
        last_price = curr_price;
        curr_price = new_price;
        
        sequence.store(seq + 2, std::memory_order_release);
    }

    // Helper to perform atomic reads
    bool read_price(double& out_curr, double& out_last) const {
        uint64_t seq1, seq2;
        do {
            seq1 = sequence.load(std::memory_order_acquire);
            if (seq1 % 2 != 0) continue; // Write in progress
            
            out_curr = curr_price;
            out_last = last_price;
            
            seq2 = sequence.load(std::memory_order_acquire);
        } while (seq1 != seq2);
        return true;
    }
};

static const char* SHM_NAME = "/exchange_market_data";

} // namespace Exchange

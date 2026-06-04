#pragma once
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include "fbs/order_generated.h"

namespace Exchange {

/**
 * @brief Manages a refreshing terminal display with client status, open orders, and execution logs.
 */
class DisplayFramework {
public:
    /**
     * @brief Appends a message to the execution log.
     * If the log reaches 10 messages, it clears and starts over from 1.
     * @param msg The message to append.
     */
    void add_message(const std::string& msg) {
        if (messages_.size() >= 10) {
            messages_.clear();
        }
        messages_.push_back(msg);
    }

    /**
     * @brief Refreshes the terminal display.
     */
    void display(const std::string& client_name, 
                 uint32_t client_id,
                 uint32_t target_symbol,
                 int64_t position,
                 int64_t cash,
                 double current_price,
                 const std::vector<OrderResponseT>& open_orders) {
        // \033[H moves cursor to top-left (home)
        std::cout << "\033[H";

        double total_value = static_cast<double>(cash) + static_cast<double>(position) * current_price;

        std::cout << "\033[K================ " << client_name << " (" << client_id << ") =================" << std::endl;
        std::cout << "\033[KTarget Symbol: " << target_symbol << std::endl;
        std::cout << "\033[KPosition:      " << position << std::endl;
        std::cout << "\033[KCash:          " << cash << std::endl;
        std::cout << "\033[KTotal Value:   " << std::fixed << std::setprecision(2) << total_value << std::endl;
        std::cout << "\033[K==========================================================" << std::endl;

        const int max_rows = 10;
        int row_count = 0;

        // 1. Display Open Orders
        for (const auto& o : open_orders) {
            if (row_count >= max_rows) break;
            std::cout << "\033[K" << std::setw(2) << std::setfill('0') << (++row_count) << " - ";
            std::cout << std::left << std::setw(5) << EnumNameSide(o.side) << " " 
                      << std::right << std::setw(6) << o.q << " @ " 
                      << std::right << std::setw(10) << o.p 
                      << " (ID: " << o.order_id << ")" << std::endl;
        }

        // 2. Display Execution Messages in remaining rows
        for (const auto& msg : messages_) {
            if (row_count >= max_rows) break;
            std::cout << "\033[K" << std::setw(2) << std::setfill('0') << (++row_count) << " - " << msg << std::endl;
        }

        // 3. Pad remaining empty rows
        while (row_count < max_rows) {
            std::cout << "\033[K" << std::setw(2) << std::setfill('0') << (++row_count) << " - " << std::endl;
        }

        std::cout << "\033[K==========================================================" << std::endl;
        // Clear anything below in case the previous display was longer
        std::cout << "\033[J" << std::flush;
    }

private:
    std::vector<std::string> messages_;
};

} // namespace Exchange

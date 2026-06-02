#pragma once
#include <map>
#include <mutex>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include "fbs/order_generated.h"

namespace Exchange {

struct L2Book {
    uint32_t symbol_id = 0;
    std::map<int64_t, uint64_t, std::greater<int64_t>> bids;
    std::map<int64_t, uint64_t> asks;
    std::mutex mutex;

    void update(Side side, int64_t price, uint64_t qty) {
        std::lock_guard<std::mutex> lock(mutex);
        if (side == Side_None) {
            bids.clear();
            asks.clear();
            return;
        }
        if (side == Side_Buy) {
            if (qty == 0) {
                bids.erase(price);
            } else {
                bids[price] = qty;
            }
        } else {
            if (qty == 0) {
                asks.erase(price);
            } else {
                asks[price] = qty;
            }
        }
    }

    void display() {
        std::lock_guard<std::mutex> lock(mutex);
        // \033[H moves cursor to top-left (home)
        std::cout << "\033[H";
        
        std::string title = "L2 Book: " + std::to_string(symbol_id);

        int depth_limit = 10;
        int bar_max_width = 20;
        int total_width = 50;
        int content_inner_width = total_width - 4; // 46

        auto print_border = [&]() {
            std::cout << std::string(total_width, '*') << std::endl;
        };

        auto print_centered = [&](const std::string& text) {
            int padding = (content_inner_width - static_cast<int>(text.length())) / 2;
            if (padding < 0) padding = 0;
            std::cout << "* " << std::string(padding, ' ') << text 
                      << std::string(content_inner_width - text.length() - padding, ' ') << " *" << std::endl;
        };

        auto print_separator = [&]() {
            std::cout << "* " << std::string(content_inner_width, '-') << " *" << std::endl;
        };

        auto print_empty_row = [&](const std::string& msg) {
            int padding = (content_inner_width - static_cast<int>(msg.length())) / 2;
            std::cout << "* " << std::string(padding, ' ') << msg 
                      << std::string(content_inner_width - msg.length() - padding, ' ') << " *" << std::endl;
        };

        // Find max qty for scaling
        uint64_t max_qty = 1; 
        std::vector<std::pair<int64_t, uint64_t>> ask_list;
        int count = 0;
        for (const auto& pair : asks) {
            ask_list.push_back(pair);
            if (pair.second > max_qty) max_qty = pair.second;
            if (++count >= depth_limit) break;
        }
        
        count = 0;
        std::vector<std::pair<int64_t, uint64_t>> bid_list;
        for (const auto& pair : bids) {
            bid_list.push_back(pair);
            if (pair.second > max_qty) max_qty = pair.second;
            if (++count >= depth_limit) break;
        }

        auto get_bar = [&](uint64_t qty) {
            int len = static_cast<int>((static_cast<double>(qty) / max_qty) * bar_max_width);
            return std::string(len, '=');
        };

        print_border();
        print_centered(title);
        print_border();

        // Print asks from highest price to lowest price
        if (ask_list.empty()) {
            print_empty_row("(No Asks)");
        } else {
            for (int i = static_cast<int>(ask_list.size()) - 1; i >= 0; --i) {
                std::string idx = "A" + std::to_string(i + 1);
                std::cout << "* " << std::left << std::setw(3) << idx << " " 
                          << std::right << std::setw(10) << ask_list[i].first << " " 
                          << std::right << std::setw(10) << ask_list[i].second << " "
                          << std::left << std::setw(bar_max_width) << get_bar(ask_list[i].second) 
                          << " *" << std::endl;
            }
        }

        print_separator();

        // Print bids
        if (bid_list.empty()) {
            print_empty_row("(No Bids)");
        } else {
            for (size_t i = 0; i < bid_list.size(); ++i) {
                std::string idx = "B" + std::to_string(i + 1);
                std::cout << "* " << std::left << std::setw(3) << idx << " " 
                          << std::right << std::setw(10) << bid_list[i].first << " " 
                          << std::right << std::setw(10) << bid_list[i].second << " "
                          << std::left << std::setw(bar_max_width) << get_bar(bid_list[i].second) 
                          << " *" << std::endl;
            }
        }

        print_border();
        // Clear anything below the book in case depth decreased
        std::cout << "\033[J" << std::flush;
    }
};

} // namespace Exchange

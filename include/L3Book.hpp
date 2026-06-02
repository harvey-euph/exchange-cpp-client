#pragma once
#include <map>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <list>
#include <iostream>
#include <iomanip>
#include <vector>
#include <sstream>
#include <string>
#include "fbs/order_generated.h"

namespace Exchange {

struct L3Order {
    uint64_t order_id;
    Side side;
    int64_t price;
    uint64_t qty;
    std::list<uint64_t>::iterator queue_pos;
};

struct L3Book {
    uint32_t symbol_id = 0;
    std::unordered_map<uint64_t, L3Order> orders;
    
    // Price levels with queue of order IDs (Price -> [order_id, ...])
    std::map<int64_t, std::list<uint64_t>, std::greater<int64_t>> bids;
    std::map<int64_t, std::list<uint64_t>> asks;
    
    std::mutex mutex;

    void update(ExecType type, uint64_t order_id, Side side, int64_t price, uint64_t qty) {
        std::lock_guard<std::mutex> lock(mutex);
        
        switch (type) {
            case ExecType_New: {
                if (orders.count(order_id)) {
                    remove_from_queues(order_id);
                }
                auto& queue = (side == Side_Buy) ? bids[price] : asks[price];
                queue.push_back(order_id);
                orders[order_id] = {order_id, side, price, qty, std::prev(queue.end())};
                break;
            }
            case ExecType_Fill:
            case ExecType_Cancelled: {
                remove_from_queues(order_id);
                orders.erase(order_id);
                break;
            }
            case ExecType_PartialFill: {
                auto it = orders.find(order_id);
                if (it != orders.end()) {
                    if (it->second.qty > qty) {
                        it->second.qty -= qty;
                    } else {
                        // This shouldn't happen if OrderBook is consistent, 
                        // but if qty_fill >= qty_remaining, it's a full fill.
                        remove_from_queues(order_id);
                        orders.erase(it);
                    }
                }
                break;
            }
            case ExecType_Replaced: {
                auto it = orders.find(order_id);
                if (it != orders.end()) {
                    if (it->second.price != price || it->second.side != side) {
                        remove_from_queues(order_id);
                        auto& queue = (side == Side_Buy) ? bids[price] : asks[price];
                        queue.push_back(order_id);
                        it->second = {order_id, side, price, qty, std::prev(queue.end())};
                    } else {
                        it->second.qty = qty;
                    }
                } else {
                    auto& queue = (side == Side_Buy) ? bids[price] : asks[price];
                    queue.push_back(order_id);
                    orders[order_id] = {order_id, side, price, qty, std::prev(queue.end())};
                }
                break;
            }
            default:
                break;
        }
    }

    void remove_from_queues(uint64_t order_id) {
        auto it = orders.find(order_id);
        if (it == orders.end()) return;

        if (it->second.side == Side_Buy) {
            auto level_it = bids.find(it->second.price);
            if (level_it != bids.end()) {
                level_it->second.erase(it->second.queue_pos);
                if (level_it->second.empty()) bids.erase(level_it);
            }
        } else if (it->second.side == Side_Sell) {
            auto level_it = asks.find(it->second.price);
            if (level_it != asks.end()) {
                level_it->second.erase(it->second.queue_pos);
                if (level_it->second.empty()) asks.erase(level_it);
            }
        }
    }

    void display(int depth_limit = 10) {
        std::lock_guard<std::mutex> lock(mutex);
        
        std::cout << "\033[2J\033[H"; // Clear screen and move to home
        
        int total_width = 80;
        int content_inner_width = total_width - 4;

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

        print_border();
        print_centered("L3 Order Book - Symbol: " + std::to_string(symbol_id));
        print_border();

        // Asks (Sorted Low to High, we print High to Low for top-down view)
        std::vector<int64_t> ask_prices;
        for (auto const& [price, _] : asks) {
            ask_prices.push_back(price);
            if (ask_prices.size() >= static_cast<size_t>(depth_limit)) break;
        }

        if (ask_prices.empty()) {
            print_empty_row("(No Asks)");
        } else {
            for (int i = static_cast<int>(ask_prices.size()) - 1; i >= 0; --i) {
                int64_t p = ask_prices[i];
                auto const& queue = asks.at(p);
                uint64_t total_q = 0;
                std::stringstream ss;
                int o_count = 0;
                for (uint64_t oid : queue) {
                    uint64_t oq = orders.at(oid).qty;
                    total_q += oq;
                    if (o_count < 5) {
                        if (o_count > 0) ss << " -> ";
                        ss << oq;
                    }
                    o_count++;
                }
                if (o_count > 5) ss << " -> ...";

                std::stringstream row;
                row << "A" << (i + 1) << " " << std::setw(10) << p << " " << std::setw(10) << total_q << " | " << ss.str();
                
                std::string row_str = row.str();
                if (row_str.length() > static_cast<size_t>(content_inner_width)) {
                    row_str = row_str.substr(0, content_inner_width - 3) + "...";
                }
                std::cout << "* " << std::left << std::setw(content_inner_width) << row_str << " *" << std::endl;
            }
        }

        print_separator();

        // Bids (Sorted High to Low)
        if (bids.empty()) {
            print_empty_row("(No Bids)");
        } else {
            int i = 0;
            for (auto const& [p, queue] : bids) {
                if (++i > depth_limit) break;
                uint64_t total_q = 0;
                std::stringstream ss;
                int o_count = 0;
                for (uint64_t oid : queue) {
                    uint64_t oq = orders.at(oid).qty;
                    total_q += oq;
                    if (o_count < 5) {
                        if (o_count > 0) ss << " -> ";
                        ss << oq;
                    }
                    o_count++;
                }
                if (o_count > 5) ss << " -> ...";

                std::stringstream row;
                row << "B" << i << " " << std::setw(10) << p << " " << std::setw(10) << total_q << " | " << ss.str();
                
                std::string row_str = row.str();
                if (row_str.length() > static_cast<size_t>(content_inner_width)) {
                    row_str = row_str.substr(0, content_inner_width - 3) + "...";
                }
                std::cout << "* " << std::left << std::setw(content_inner_width) << row_str << " *" << std::endl;
            }
        }

        print_border();
        std::cout << std::flush;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        orders.clear();
        bids.clear();
        asks.clear();
    }
};

} // namespace Exchange

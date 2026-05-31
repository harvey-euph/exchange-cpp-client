#include "AlgoTradingClient.hpp"
#include <iostream>
#include <map>
#include <iomanip>
#include <vector>
#include <mutex>

namespace Exchange {

class L2OrderBookClient : public AlgoTradingClient {
public:
    L2OrderBookClient(const Config& config) : AlgoTradingClient(config) {
        // Use a small trick to clear screen initially
        std::cout << "\033[2J\033[H" << std::flush;
    }

    void on_l2_update(const L2Update* update) override {
        // Snapshot markers often have side = None or price/qty = 0
        if (update->side() == Side_None) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        if (update->side() == Side_Buy) {
            if (update->q() == 0) {
                bids_.erase(update->p());
            } else {
                bids_[update->p()] = update->q();
            }
        } else if (update->side() == Side_Sell) {
            if (update->q() == 0) {
                asks_.erase(update->p());
            } else {
                asks_[update->p()] = update->q();
            }
        }
        
        display();
    }

    // Unused overrides
    void on_l3_update(const L3Update*) override {}
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

private:
    void display() {
        // \033[H moves cursor to top-left (home)
        std::cout << "\033[H";
        
        uint32_t symbol = config_.symbol_ids.empty() ? 0 : config_.symbol_ids[0];
        std::string title = "L2 Book: " + std::to_string(symbol);

        int depth_limit = 10;
        int bar_max_width = 20;
        // Total width calculation: 
        // border("* ") [2] + index [3] + sp [1] + price [10] + sp [1] + qty [10] + sp [1] + bar [20] + border(" *") [2]
        // = 2 + 3 + 1 + 10 + 1 + 10 + 1 + 20 + 2 = 50
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
        for (const auto& pair : asks_) {
            ask_list.push_back(pair);
            if (pair.second > max_qty) max_qty = pair.second;
            if (++count >= depth_limit) break;
        }
        
        count = 0;
        std::vector<std::pair<int64_t, uint64_t>> bid_list;
        for (const auto& pair : bids_) {
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

    // Bids: High to Low
    std::map<int64_t, uint64_t, std::greater<int64_t>> bids_;
    // Asks: Low to High
    std::map<int64_t, uint64_t> asks_;
    std::mutex mutex_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L2OrderBookClient client(config);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

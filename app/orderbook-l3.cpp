#include "AlgoTradingClient.hpp"
#include <iostream>
#include <map>
#include <iomanip>
#include <vector>
#include <mutex>
#include <list>
#include <unordered_map>
#include <sstream>

namespace Exchange {

struct L3OrderInfo {
    uint64_t id;
    int64_t price;
    uint64_t qty;
    Side side;
};

class L3OrderBookClient : public AlgoTradingClient {
public:
    L3OrderBookClient(const Config& config) : AlgoTradingClient(config) {
        std::cout << "\033[2J\033[H" << std::flush;
    }

    void on_l3_update(const L3Update* update) override {
        if (update->side() == Side_None) return;

        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t oid = update->order_id();
        int64_t price = update->p();
        uint64_t qty = update->q();
        Side side = update->side();
        ExecType type = update->exec_type();

        switch (type) {
            case ExecType_New: {
                orders_[oid] = {oid, price, qty, side};
                auto& level = (side == Side_Buy) ? bids_[price] : asks_[price];
                level.push_back(oid);
                break;
            }
            case ExecType_PartialFill: {
                if (orders_.count(oid)) {
                    orders_[oid].qty = qty; // Assuming q is the new remaining qty
                }
                break;
            }
            case ExecType_Fill:
            case ExecType_Cancelled: {
                if (orders_.count(oid)) {
                    auto& info = orders_[oid];
                    auto& level = (info.side == Side_Buy) ? bids_[info.price] : asks_[info.price];
                    level.remove(oid);
                    if (level.empty()) {
                        if (info.side == Side_Buy) bids_.erase(info.price);
                        else asks_.erase(info.price);
                    }
                    orders_.erase(oid);
                }
                break;
            }
            case ExecType_Replaced: {
                // Remove old
                if (orders_.count(oid)) {
                    auto& info = orders_[oid];
                    auto& level = (info.side == Side_Buy) ? bids_[info.price] : asks_[info.price];
                    level.remove(oid);
                    if (level.empty()) {
                        if (info.side == Side_Buy) bids_.erase(info.price);
                        else asks_.erase(info.price);
                    }
                }
                // Add new
                orders_[oid] = {oid, price, qty, side};
                auto& level = (side == Side_Buy) ? bids_[price] : asks_[price];
                level.push_back(oid);
                break;
            }
        }
        
        display();
    }

    // Unused overrides
    void on_l2_update(const L2Update*) override {}
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

private:
    void display() {
        std::cout << "\033[H";
        
        uint32_t symbol = config_.symbol_ids.empty() ? 0 : config_.symbol_ids[0];
        std::string title = "L3 Book: " + std::to_string(symbol);

        int depth_limit = 10; // L3 can be very verbose, limit depth for display
        int total_width = 80; // Wider for L3 queues
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
        print_centered(title);
        print_border();

        // Prepare Asks (Sorted Low to High, we print High to Low)
        std::vector<int64_t> ask_prices;
        for (auto const& [price, _] : asks_) {
            ask_prices.push_back(price);
            if (ask_prices.size() >= static_cast<size_t>(depth_limit)) break;
        }

        if (ask_prices.empty()) {
            print_empty_row("(No Asks)");
        } else {
            for (int i = static_cast<int>(ask_prices.size()) - 1; i >= 0; --i) {
                int64_t p = ask_prices[i];
                auto& queue = asks_[p];
                uint64_t total_q = 0;
                std::stringstream ss;
                int o_count = 0;
                for (uint64_t oid : queue) {
                    uint64_t oq = orders_[oid].qty;
                    total_q += oq;
                    if (o_count < 5) { // Show first 5 orders
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

        // Prepare Bids (Sorted High to Low)
        int b_count = 0;
        if (bids_.empty()) {
            print_empty_row("(No Bids)");
        } else {
            for (auto const& [p, queue] : bids_) {
                uint64_t total_q = 0;
                std::stringstream ss;
                int o_count = 0;
                for (uint64_t oid : queue) {
                    uint64_t oq = orders_[oid].qty;
                    total_q += oq;
                    if (o_count < 5) {
                        if (o_count > 0) ss << " -> ";
                        ss << oq;
                    }
                    o_count++;
                }
                if (o_count > 5) ss << " -> ...";

                std::stringstream row;
                row << "B" << (b_count + 1) << " " << std::setw(10) << p << " " << std::setw(10) << total_q << " | " << ss.str();
                
                std::string row_str = row.str();
                if (row_str.length() > static_cast<size_t>(content_inner_width)) {
                    row_str = row_str.substr(0, content_inner_width - 3) + "...";
                }

                std::cout << "* " << std::left << std::setw(content_inner_width) << row_str << " *" << std::endl;

                if (++b_count >= depth_limit) break;
            }
        }

        print_border();
        std::cout << "\033[J" << std::flush;
    }

    std::map<int64_t, std::list<uint64_t>, std::greater<int64_t>> bids_;
    std::map<int64_t, std::list<uint64_t>> asks_;
    std::unordered_map<uint64_t, L3OrderInfo> orders_;
    std::mutex mutex_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L3OrderBookClient client(config);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

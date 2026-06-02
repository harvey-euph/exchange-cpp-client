#include "AlgoTradingClient.hpp"
#include "ClientAccount.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <mutex>

namespace Exchange {

class AccountMonitor : public AlgoTradingClient {
public:
    AccountMonitor(const Config& config) : AlgoTradingClient(config) {}

    void on_l2_update(const L2Update* /*update*/) override {}
    void on_l3_update(const L3Update* /*update*/) override {}

    void on_order_response(const OrderResponse* response) override {
        account_.handle_order_response(response);
        needs_display_ = true;
    }

    void on_position_response(const PositionResponse* response) override {
        account_.handle_position_response(response);
        needs_display_ = true;
    }

    void start_display_thread() {
        display_thread_ = std::thread([this]() {
            while (running_) {
                if (needs_display_) {
                    display();
                    needs_display_ = false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }

    void stop() {
        AlgoTradingClient::stop();
        if (display_thread_.joinable()) {
            display_thread_.join();
        }
    }

private:
    void display() {
        // \033[H moves cursor to top-left, \033[J clears screen from cursor down
        std::cout << "\033[H\033[J";
        std::cout << "========== Account Monitor (Client ID: " << config_.client_id << ") ==========" << std::endl;
        
        // Display Cash
        std::cout << "\n[Account Balance]" << std::endl;
        std::cout << "  Cash: " << account_.get_cash() << std::endl;

        // Display Positions
        std::cout << "\n[Positions]" << std::endl;
        auto positions = account_.get_all_positions();
        bool has_positions = false;
        for (const auto& [symbol_id, qty] : positions) {
            if (symbol_id == 0) continue; // Skip cash in the positions list
            if (!has_positions) {
                std::cout << "  " << std::left << std::setw(10) << "Symbol ID" << std::right << std::setw(15) << "Quantity" << std::endl;
                has_positions = true;
            }
            std::cout << "  " << std::left << std::setw(10) << symbol_id << std::right << std::setw(15) << qty << std::endl;
        }
        if (!has_positions) {
            std::cout << "  (No symbol positions)" << std::endl;
        }

        // Display Open Orders
        std::cout << "\n[Open Orders]" << std::endl;
        auto orders = account_.get_open_orders();
        if (orders.empty()) {
            std::cout << "  (No open orders)" << std::endl;
        } else {
            std::cout << "  " << std::left << std::setw(12) << "Order ID" 
                      << std::setw(10) << "Symbol" 
                      << std::setw(8) << "Side" 
                      << std::right << std::setw(12) << "Price" 
                      << std::setw(12) << "Quantity" << std::endl;
            for (const auto& order : orders) {
                std::cout << "  " << std::left << std::setw(12) << order.order_id 
                          << std::setw(10) << order.symbol_id 
                          << std::setw(8) << (order.side == Side_Buy ? "BUY" : "SELL")
                          << std::right << std::setw(12) << order.p 
                          << std::setw(12) << order.q << std::endl;
            }
        }
        std::cout << "\n============================================================" << std::endl;
        std::cout << std::flush;
    }

    ClientAccount account_;
    std::thread display_thread_;
    std::atomic<bool> needs_display_{true};
};

} // namespace Exchange

int main() {
    Exchange::AlgoTradingConfig config;
    config.client_id = 101;
    config.symbol_ids = {1, 2}; // Monitor symbols 1 and 2

    Exchange::AccountMonitor monitor(config);
    monitor.start_display_thread();
    
    std::cout << "Starting Account Monitor..." << std::endl;
    return monitor.run();
}

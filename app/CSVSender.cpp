#include "AlgoTradingClient.hpp"
#include "csv_util.hpp"
#include "LogUtil.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstdlib>
#include <vector>

namespace Exchange {

class CSVSender : public AlgoTradingClient {
public:
    CSVSender(const Config& config, const std::string& csv_path)
        : AlgoTradingClient(config), csv_path_(csv_path) {
        if (!reader_.loadFromCSV(csv_path_)) {
            throw std::runtime_error("Failed to load CSV: " + csv_path_);
        }
        std::cout << "[CSVSender] Loaded " << reader_.getRequests().size() << " orders from " << csv_path_ << std::endl;
    }

    void on_l2_update(const L2Update* update) override {
        (void)update;
    }

    void on_l3_update(const L3Update* update) override {
        (void)update;
    }

    void on_order_response(const OrderResponse* response) override {
        logOrderResponse(response, "[CSVSender]");
    }

    void on_position_response(const PositionResponse* response) override {
        logPositionResponse(response, "[CSVSender]");
    }

    void on_timer() override {
        if (!is_ready()) {
            static bool waiting_logged = false;
            if (!waiting_logged) {
                std::cout << "[CSVSender] Waiting for server ready signal..." << std::endl;
                waiting_logged = true;
            }
            return;
        }

        const auto& requests = reader_.getRequests();
        if (current_idx_ < requests.size()) {
            OrderRequestT req;
            requests[current_idx_]->UnPackTo(&req);
            
            send_order_request(req);
            std::cout << "[CSVSender] Sent order (" << current_idx_ + 1 << "/" << requests.size() << "): action=" << EnumNameOrderAction(req.action)
                      << ", symbol=" << req.symbol_id << ", side=" << EnumNameSide(req.side)
                      << ", p=" << req.p << ", q=" << req.q << std::endl;
            
            current_idx_++;
        } else {
            if (!finished_) {
                std::cout << "[CSVSender] Finished sending orders. Waiting for remaining responses..." << std::endl;
                finished_ = true;
                finish_start_time_ = std::chrono::steady_clock::now();
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - finish_start_time_).count() >= 5) {
                std::cout << "[CSVSender] Stopping." << std::endl;
                stop();
            }
        }
    }

private:
    std::string csv_path_;
    CSVDataReader reader_;
    size_t current_idx_ = 0;
    bool finished_ = false;
    std::chrono::steady_clock::time_point finish_start_time_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    std::string csv_path = "data/basic.csv";
    if (argc > 1) {
        csv_path = argv[1];
    }

    Exchange::AlgoTradingConfig config;
    config.use_http = true;
    config.timer_interval_ms = 50;
    
    try {
        Exchange::CSVSender client(config, csv_path);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "[CSVSender] Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

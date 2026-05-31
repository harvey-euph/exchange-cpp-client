#include "AlgoTradingClient.hpp"
#include "csv_util.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <cstdlib>

namespace Exchange {

class CSVAlgoClient : public AlgoTradingClient {
public:
    CSVAlgoClient(const Config& config, const std::string& csv_path)
        : AlgoTradingClient(config), csv_path_(csv_path) {}

    void on_l2_update(const L2Update* update) override {
        // Silently consume L2 updates or add logging if needed
        (void)update;
    }

    void on_l3_update(const L3Update* update) override {
        // Silently consume L3 updates or add logging if needed
        (void)update;
    }

    void on_order_response(const OrderResponse* response) override {
        std::cout << "[CSVAlgoClient] Order Response | ID: " << response->order_id()
                  << " | Exec: " << EnumNameExecType(response->exec_type())
                  << " | Side: " << EnumNameSide(response->side())
                  << " | Price: " << response->p()
                  << " | Qty: " << response->q()
                  << " | Reject: " << EnumNameRejectCode(response->reject_code()) << std::endl;
    }

    void on_position_response(const PositionResponse* response) override {
        std::cout << "[CSVAlgoClient] Position Response | Symbol: " << response->symbol_id()
                  << " | Position: " << response->position() << std::endl;
    }

    void start_csv_processing() {
        processing_thread_ = std::thread([this]() {
            // Give some time for WebSocket connections to stabilize
            std::this_thread::sleep_for(std::chrono::seconds(1));

            CSVDataReader reader;
            if (!reader.loadFromCSV(csv_path_)) {
                std::cerr << "[CSVAlgoClient] Failed to load CSV: " << csv_path_ << std::endl;
                stop();
                return;
            }

            const auto& requests = reader.getRequests();
            std::cout << "[CSVAlgoClient] Loaded " << requests.size() << " orders from " << csv_path_ << std::endl;

            for (const auto* order : requests) {
                if (!running_) break;

                OrderRequestT req;
                order->UnPackTo(&req);
                
                // AlgoTradingClient::send_order_request will fill in:
                // - client_id (from config)
                // - timestamp (current time)
                // - order_id/exec_id (auto-incrementing if it's a New order)
                
                send_order_request(req);
                std::cout << "[CSVAlgoClient] Sent order: action=" << EnumNameOrderAction(req.action)
                          << ", symbol=" << req.symbol_id << ", side=" << EnumNameSide(req.side)
                          << ", p=" << req.p << ", q=" << req.q << std::endl;

                // Throttling to simulate more realistic message flow
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::cout << "[CSVAlgoClient] Finished sending orders. Waiting 5s for remaining responses..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            stop();
        });
    }

    ~CSVAlgoClient() {
        if (processing_thread_.joinable()) processing_thread_.join();
    }

private:
    std::string csv_path_;
    std::thread processing_thread_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    std::string csv_path = "data/basic.csv";
    if (argc > 1) {
        csv_path = argv[1];
    }

    Exchange::AlgoTradingConfig config;
    config.use_http = true;
    // Default config uses 127.0.0.1 and ports 9001, 9002, 9003
    
    try {
        Exchange::CSVAlgoClient client(config, csv_path);
        client.start_csv_processing();
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "[CSVAlgoClient] Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}

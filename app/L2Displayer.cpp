#include "AlgoTradingClient.hpp"
#include "L2Book.hpp"
#include <iostream>

namespace Exchange {

class L2Displayer : public AlgoTradingClient {
public:
    L2Displayer(const Config& config) : AlgoTradingClient(config) {
        if (!config_.symbol_ids.empty()) {
            book_.symbol_id = config_.symbol_ids[0];
        }
    }

    void on_l2_update(const L2Update* update) override {
        book_.update(update->side(), update->p(), update->q());
        book_.display();
    }

    // Unused overrides
    void on_l3_update(const L3Update*) override {}
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

private:
    L2Book book_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L2Displayer client(config);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

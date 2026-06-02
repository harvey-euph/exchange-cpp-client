#include "AlgoTradingClient.hpp"
#include "L3Book.hpp"
#include <iostream>

namespace Exchange {

class L3Displayer : public AlgoTradingClient {
public:
    L3Displayer(const Config& config) : AlgoTradingClient(config) {
        if (!config_.symbol_ids.empty()) {
            book_.symbol_id = config_.symbol_ids[0];
        }
    }

    void on_l3_update(const L3Update* update) override {
        book_.update(update->exec_type(), update->order_id(), update->side(), update->p(), update->q());
        book_.display();
    }

    // Unused overrides
    void on_l2_update(const L2Update*) override {}
    void on_order_response(const OrderResponse*) override {}
    void on_position_response(const PositionResponse*) override {}

private:
    L3Book book_;
};

} // namespace Exchange

int main(int argc, char** argv) {
    Exchange::AlgoTradingConfig config;
    if (argc > 1) {
        config.symbol_ids = { static_cast<uint32_t>(std::stoul(argv[1])) };
    }

    try {
        Exchange::L3Displayer client(config);
        return client.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

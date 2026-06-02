#include "AlgoTradingClient.hpp"
#include "LogUtil.hpp"
#include <iostream>

namespace Exchange {

class StdoutClient : public AlgoTradingClient {
public:
    using AlgoTradingClient::AlgoTradingClient;

    void on_l2_update(const L2Update* update) override {
        logL2Update(update, "[StdoutClient]");
    }

    void on_l3_update(const L3Update* update) override {
        logL3Update(update, "[StdoutClient]");
    }

    void on_order_response(const OrderResponse* response) override {
        logOrderResponse(response, "[StdoutClient]");
    }

    void on_position_response(const PositionResponse* response) override {
        logPositionResponse(response, "[StdoutClient]");
    }
};

} // namespace Exchange

int main() {
    Exchange::StdoutClient cli1;
    return cli1.run();
}

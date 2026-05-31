#include "AlgoTradingClient.hpp"
#include <iostream>

namespace Exchange {

class StdoutClient : public AlgoTradingClient {
public:
    using AlgoTradingClient::AlgoTradingClient;

    void on_l2_update(const L2Update* update) override {
        if (update->side() == Side_None) {
            std::cout << "[StdoutClient] L2 Snapshot Start for symbol " << update->symbol_id() << std::endl;
            return;
        }
        std::cout << "[StdoutClient] L2 Update | Symbol: " << update->symbol_id()
                  << " | Side: " << EnumNameSide(update->side())
                  << " | Price: " << update->p()
                  << " | Qty: " << update->q() << std::endl;
    }

    void on_l3_update(const L3Update* update) override {
        if (update->side() == Side_None) {
            std::cout << "[StdoutClient] L3 Snapshot Start for symbol " << update->symbol_id() << std::endl;
            return;
        }
        std::cout << "[StdoutClient] L3 Update | Symbol: " << update->symbol_id()
                  << " | Type: " << EnumNameExecType(update->exec_type())
                  << " | ID: " << update->order_id()
                  << " | Price: " << update->p()
                  << " | Qty: " << update->q() << std::endl;
    }

    void on_order_response(const OrderResponse* response) override {
        std::cout << "[StdoutClient] Order Response | ID: " << response->order_id()
                  << " | Exec: " << EnumNameExecType(response->exec_type())
                  << " | Reject: " << EnumNameRejectCode(response->reject_code()) << std::endl;
    }

    void on_position_response(const PositionResponse* response) override {
        std::cout << "[StdoutClient] Position Response | Symbol: " << response->symbol_id()
                  << " | Position: " << response->position() << std::endl;
    }
};

} // namespace Exchange

int main() {
    Exchange::StdoutClient cli1;
    return cli1.run();
}

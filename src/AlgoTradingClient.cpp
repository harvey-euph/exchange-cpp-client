#include "AlgoTradingClient.hpp"
#include <iostream>
#include <chrono>
#include <thread>

namespace Exchange {

AlgoTradingClient::AlgoTradingClient(const Config& config) : config_(config) {
    mgmt_client_ = SimpleWSClient::create(config_.host, config_.mgmt_port);
    l2_client_ = SimpleWSClient::create(config_.host, config_.l2_port);
    l3_client_ = SimpleWSClient::create(config_.host, config_.l3_port);
}

AlgoTradingClient::~AlgoTradingClient() {
    stop();
}

void AlgoTradingClient::new_limit_order(uint32_t symbol_id, Side side, int64_t p, uint64_t q, uint64_t visible_qty) {
    new_order(symbol_id, side, OrderType_Limit, p, q, visible_qty);
}

void AlgoTradingClient::new_market_order(uint32_t symbol_id, Side side, uint64_t q) {
    new_order(symbol_id, side, OrderType_Market, 0, q, q);
}

void AlgoTradingClient::new_order(uint32_t symbol_id, Side side, OrderType type, int64_t p, uint64_t q, uint64_t visible_qty) {
    OrderRequestT req;
    req.action = OrderAction_New;
    req.symbol_id = symbol_id;
    req.side = side;
    req.type = type;
    req.p = p;
    req.q = q;
    req.visible_qty = (visible_qty == 0) ? q : visible_qty;
    send_order_request(req);
}

void AlgoTradingClient::replace_order(uint64_t order_id, int64_t p, uint64_t q, uint32_t symbol_id, Side side) {
    OrderRequestT req;
    req.action = OrderAction_Modify;
    req.order_id = order_id;
    req.symbol_id = symbol_id;
    req.side = side;
    req.p = p;
    req.q = q;
    send_order_request(req);
}

void AlgoTradingClient::cancel_order(uint64_t order_id, uint32_t symbol_id, Side side) {
    OrderRequestT req;
    req.action = OrderAction_Cancel;
    req.order_id = order_id;
    req.symbol_id = symbol_id;
    req.side = side;
    send_order_request(req);
}

void AlgoTradingClient::send_order_request(OrderRequestT& order) {
    order.client_id = config_.client_id;
    if (order.action == OrderAction_New) {
        order.order_id = next_id_++;
        order.exec_id = order.order_id;
    } else {
        order.exec_id = next_id_++;
    }

    order.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    flatbuffers::FlatBufferBuilder fbb(256);
    auto order_offset = OrderRequest::Pack(fbb, &order);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_OrderRequest, order_offset.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

void AlgoTradingClient::query_position(uint32_t symbol_id) {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto pos_req = CreatePositionRequest(fbb, config_.client_id, symbol_id);
    auto client_req = CreateClientRequest(fbb, ClientRequestData_PositionRequest, pos_req.Union());
    fbb.Finish(client_req);
    mgmt_client_->send(fbb.GetBufferPointer(), fbb.GetSize());
}

int AlgoTradingClient::run() {
    if (!mgmt_client_->connect()) {
        std::cerr << "Failed to connect to Management port " << config_.mgmt_port << std::endl;
        return 1;
    }
    if (!l2_client_->connect()) {
        std::cerr << "Failed to connect to L2 port " << config_.l2_port << std::endl;
        return 1;
    }
    if (!l3_client_->connect()) {
        std::cerr << "Failed to connect to L3 port " << config_.l3_port << std::endl;
        return 1;
    }

    mgmt_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto resp = flatbuffers::GetRoot<ClientResponse>(data);
        if (resp->data_type() == ClientResponseData_OrderResponse) {
            on_order_response(resp->data_as_OrderResponse());
        } else if (resp->data_type() == ClientResponseData_PositionResponse) {
            on_position_response(resp->data_as_PositionResponse());
        }
    });

    l2_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto update = flatbuffers::GetRoot<L2Update>(data);
        on_l2_update(update);
    });

    l3_client_->run_async([this](const void* data, size_t size) {
        (void)size;
        auto update = flatbuffers::GetRoot<L3Update>(data);
        on_l3_update(update);
    });

    // Subscriptions
    mgmt_client_->send_text("sub " + std::to_string(config_.client_id));
    for (auto sym : config_.symbol_ids) {
        l2_client_->send_text("sub " + std::to_string(sym));
        l3_client_->send_text("sub " + std::to_string(sym));
    }

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}

void AlgoTradingClient::stop() {
    running_ = false;
    if (mgmt_client_) mgmt_client_->stop();
    if (l2_client_) l2_client_->stop();
    if (l3_client_) l3_client_->stop();
}

} // namespace Exchange

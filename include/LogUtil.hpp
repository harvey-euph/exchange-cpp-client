#pragma once
#include <iostream>
#include "fbs/order_generated.h"
// #include "Order.hpp"

namespace Exchange {

inline void logOrderRequest(const OrderRequest* req, const char* prefix = "[OrderRequest]") {
    if (!req) return;
    std::cout << prefix << " "
              << "action=" << EnumNameOrderAction(req->action())
              << ", exec_id=" << req->exec_id()
              << ", order_id=" << req->order_id()
              << ", client=" << req->client_id()
              << ", sym=" << req->symbol_id()
              << ", side=" << EnumNameSide(req->side())
              << ", type=" << EnumNameOrderType(req->type())
              << ", price=" << req->p()
              << ", qty=" << req->q()
              << ", visible=" << req->visible_qty()
              << ", ts=" << req->timestamp()
              << std::endl;
}

inline void logOrderResponse(const OrderResponse* resp, const char* prefix = "[OrderResponse]") {
    if (!resp) return;
    std::cout << prefix << " "
              << "exec_type=" << EnumNameExecType(resp->exec_type())
              << ", order_id=" << resp->order_id()
              << ", client=" << resp->client_id()
              << ", exec_id=" << resp->exec_id()
              << ", symbol=" << resp->symbol_id()
              << ", side=" << EnumNameSide(resp->side())
              << ", p=" << resp->p()
              << ", q=" << resp->q()
              << ", reject=" << EnumNameRejectCode(resp->reject_code())
              << std::endl;
}

/*
inline void logOrder(const Order* o, const char* prefix = "[Order]") {
    if (!o) return;
    std::cout << prefix << " "
              << "id=" << o->order_id
              << ", client=" << o->client_id
              << ", exec_id=" << o->exec_id
              << ", type=" << EnumNameOrderType(o->type)
              << ", qty_orig=" << o->qty_original
              << ", qty_rem=" << o->qty_remaining
              << ", ts=" << o->timestamp
              << (o->price_level ? " [InBook]" : " [Floating]")
              << std::endl;
}
*/

inline void logPositionResponse(const PositionResponse* resp, const char* prefix = "[PositionResponse]") {
    if (!resp) return;
    std::cout << prefix << " "
              << "client=" << resp->client_id()
              << ", symbol=" << resp->symbol_id()
              << ", position=" << resp->position()
              << std::endl;
}

inline void logL2Update(const L2Update* update, const char* prefix = "[L2Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        std::cout << prefix << " Snapshot Start | Symbol: " << update->symbol_id() << std::endl;
        return;
    }
    std::cout << prefix << " "
              << "symbol=" << update->symbol_id()
              << ", side=" << EnumNameSide(update->side())
              << ", price=" << update->p()
              << ", qty=" << update->q()
              << ", seq=" << update->seq_num()
              << ", ts=" << update->timestamp()
              << std::endl;
}

inline void logL3Update(const L3Update* update, const char* prefix = "[L3Update]") {
    if (!update) return;
    if (update->side() == Side_None) {
        std::cout << prefix << " Snapshot Start | Symbol: " << update->symbol_id() << std::endl;
        return;
    }
    std::cout << prefix << " "
              << "symbol=" << update->symbol_id()
              << ", type=" << EnumNameExecType(update->exec_type())
              << ", order_id=" << update->order_id()
              << ", side=" << EnumNameSide(update->side())
              << ", price=" << update->p()
              << ", qty=" << update->q()
              << ", seq=" << update->seq_num()
              << ", ts=" << update->timestamp()
              << std::endl;
}

} // namespace Exchange

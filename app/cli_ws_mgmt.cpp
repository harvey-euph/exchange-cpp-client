#include "SimpleWSClient.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "fbs/order_generated.h"

using namespace Exchange;

void print_client_response(const ClientResponse* resp) {
    auto type = resp->data_type();
    std::cout << "[Mgmt Response] Type: " << EnumNameClientResponseData(type) << std::endl;

    switch (type) {
        case ClientResponseData_OrderResponse: {
            auto or_resp = resp->data_as_OrderResponse();
            std::cout << "  -> Order ID: " << or_resp->order_id() 
                      << " | Exec Type: " << EnumNameExecType(or_resp->exec_type())
                      << " | Reject: " << EnumNameRejectCode(or_resp->reject_code()) << std::endl;
            break;
        }
        case ClientResponseData_PositionResponse: {
            auto pos_resp = resp->data_as_PositionResponse();
            std::cout << "  -> Client: " << pos_resp->client_id()
                      << " | Symbol: " << pos_resp->symbol_id()
                      << " | Position: " << pos_resp->position() << std::endl;
            break;
        }
        default:
            break;
    }
}

int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "9001";
        uint32_t client_id = 101;

        if (argc > 1) host = argv[1];
        if (argc > 2) port = argv[2];
        if (argc > 3) client_id = std::stoul(argv[3]);

        auto client = SimpleWSClient::create(host, port);
        
        if (!client->connect()) {
            return 1;
        }

        std::cout << "[Mgmt Client] Connected to " << host << ":" << port << std::endl;

        client->run_async([](const void* data, size_t size) {
            (void) size;
            auto client_resp = flatbuffers::GetRoot<ClientResponse>(data);
            print_client_response(client_resp);
        });

        // "Login"
        client->send_text("sub " + std::to_string(client_id));
        std::cout << "[Mgmt Client] Logged in as client " << client_id << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Send a Position Request (Cash, Symbol 0)
        {
            flatbuffers::FlatBufferBuilder fbb(128);
            auto pos_req = CreatePositionRequest(fbb, client_id, 0);
            auto client_req = CreateClientRequest(fbb, ClientRequestData_PositionRequest, pos_req.Union());
            fbb.Finish(client_req);
            client->send(fbb.GetBufferPointer(), fbb.GetSize());
            std::cout << "[Mgmt Client] Sent Cash Inquiry" << std::endl;
        }

        // Send a New Order Request
        {
            flatbuffers::FlatBufferBuilder fbb(256);
            auto order_req = CreateOrderRequest(fbb, 
                OrderAction_New, 999, 0, client_id, 1, Side_Buy, 
                OrderType_Limit, 10500, 100, 0, 1000000);
            auto client_req = CreateClientRequest(fbb, ClientRequestData_OrderRequest, order_req.Union());
            fbb.Finish(client_req);
            client->send(fbb.GetBufferPointer(), fbb.GetSize());
            std::cout << "[Mgmt Client] Sent New Order" << std::endl;
        }

        // Keep main thread alive
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (std::exception const& e) {
        std::cerr << "[Main Error] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

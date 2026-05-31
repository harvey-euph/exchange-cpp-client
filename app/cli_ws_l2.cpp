#include "SimpleWSClient.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "fbs/order_generated.h"

using namespace Exchange;

void print_l2_update(const L2Update* l2) {
    std::string side_str = (l2->side() == Side_Buy) ? "BUY" : (l2->side() == Side_Sell ? "SELL" : "NONE");
    std::cout << "[L2 Update] Seq: " << l2->seq_num() 
              << " | Symbol: " << l2->symbol_id()
              << " | " << side_str 
              << " | Price: " << l2->p() 
              << " | Qty: " << l2->q() << std::endl;
}

int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "9002";
        uint32_t symbol_id = 1;

        if (argc > 1) host = argv[1];
        if (argc > 2) port = argv[2];
        if (argc > 3) symbol_id = std::stoul(argv[3]);

        auto client = SimpleWSClient::create(host, port);
        
        if (!client->connect()) {
            return 1;
        }

        std::cout << "[L2 Client] Connected to " << host << ":" << port << std::endl;

        client->run_async([](const void* data, size_t size) {
            (void) size;
            auto l2_update = flatbuffers::GetRoot<L2Update>(data);
            if (l2_update->side() == Side_None) {
                std::cout << "[L2 Client] Received Empty Frame (Snapshot Start)" << std::endl;
            } else {
                print_l2_update(l2_update);
            }
        });

        client->send_text("sub " + std::to_string(symbol_id));
        std::cout << "[L2 Client] Subscribed to symbol " << symbol_id << std::endl;

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (std::exception const& e) {
        std::cerr << "[Main Error] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

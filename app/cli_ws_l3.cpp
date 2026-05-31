#include "SimpleWSClient.hpp"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "fbs/order_generated.h"

using namespace Exchange;

void print_l3_update(const L3Update* l3) {
    std::cout << "[L3 Update] Seq: " << l3->seq_num() 
              << " | Symbol: " << l3->symbol_id()
              << " | Type: " << EnumNameExecType(l3->exec_type())
              << " | ID: " << l3->order_id()
              << " | Side: " << EnumNameSide(l3->side())
              << " | Price: " << l3->p()
              << " | Qty: " << l3->q() << std::endl;
}

int main(int argc, char** argv) {
    try {
        std::string host = "127.0.0.1";
        std::string port = "9003";
        uint32_t symbol_id = 1;

        if (argc > 1) host = argv[1];
        if (argc > 2) port = argv[2];
        if (argc > 3) symbol_id = std::stoul(argv[3]);

        auto client = SimpleWSClient::create(host, port);
        
        if (!client->connect()) {
            return 1;
        }

        std::cout << "[L3 Client] Connected to " << host << ":" << port << std::endl;

        client->run_async([](const void* data, size_t size) {
            (void) size;
            auto l3_update = flatbuffers::GetRoot<L3Update>(data);
            if (l3_update->side() == Side_None) {
                std::cout << "[L3 Client] Received Empty Frame (Snapshot Start)" << std::endl;
            } else {
                print_l3_update(l3_update);
            }
        });

        client->send_text("sub " + std::to_string(symbol_id));
        std::cout << "[L3 Client] Subscribed to symbol " << symbol_id << std::endl;

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (std::exception const& e) {
        std::cerr << "[Main Error] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

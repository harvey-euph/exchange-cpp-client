#include "csv_util.hpp"
#include "fbs/order_generated.h"
#include "LogUtil.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

using namespace Exchange;

void send_http_order(const std::string& host, const std::string& port, const OrderRequest* order) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve(host, port);
        stream.connect(results);

        // Convert to OrderRequestT for Packing (as in AlgoTradingClient example)
        OrderRequestT req_t;
        order->UnPackTo(&req_t);

        flatbuffers::FlatBufferBuilder fbb(512);
        auto order_offset = OrderRequest::Pack(fbb, &req_t);
        fbb.Finish(order_offset);

        http::request<http::vector_body<char>> req{http::verb::post, "/order", 11};
        req.set(http::field::host, host);
        req.set(http::field::content_type, "application/octet-stream");
        
        auto const* data = reinterpret_cast<const char*>(fbb.GetBufferPointer());
        req.body().assign(data, data + fbb.GetSize());
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        std::cout << "Response: " << res.result_int() << " " << res.reason() << std::endl;

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char** argv) {
    std::string csv_path = "data/basic.csv";
    std::string host = "127.0.0.1";
    std::string port = "8080";

    if (argc > 1) csv_path = argv[1];
    if (argc > 2) host = argv[2];
    if (argc > 3) port = argv[3];

    CSVDataReader reader;
    if (!reader.loadFromCSV(csv_path)) {
        std::cerr << "Failed to load CSV: " << csv_path << std::endl;
        return 1;
    }

    const auto& requests = reader.getRequests();
    std::cout << "Loaded " << requests.size() << " orders from " << csv_path << std::endl;

    for (const auto* order : requests) {
        logOrderRequest(order, "[HTTP Sender]");
        send_http_order(host, port, order);
    }

    return 0;
}

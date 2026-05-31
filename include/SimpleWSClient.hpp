#pragma once
#include <string>
#include <functional>
#include <memory>

namespace Exchange {

/**
 * @brief 抽象的 WebSocket 客戶端 (用於 cli_*)
 */
class SimpleWSClient {
public:
    using MessageHandler = std::function<void(const void* data, size_t size)>;

    static std::unique_ptr<SimpleWSClient> create(const std::string& host, const std::string& port);

    virtual ~SimpleWSClient() = default;

    virtual bool connect() = 0;
    virtual void run_async(MessageHandler on_message) = 0;
    
    virtual void send(const void* data, size_t size) = 0;
    virtual void send_text(const std::string& text) = 0;

    virtual void stop() = 0;
};

} // namespace Exchange

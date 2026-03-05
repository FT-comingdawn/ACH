#pragma once
#include <asio.hpp>
#include <asio/strand.hpp>
#include <memory>
#include <span>

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>

class WsConnection : public std::enable_shared_from_this<WsConnection> {
public:
    WsConnection(asio::ip::tcp::socket socket, class WebServer& server);

    void Start();

    void SendText(std::string text);
    void SendBinary(std::span<const char> data);
    void Close(uint16_t code = 1000, std::string reason = "");

    std::function<void(std::string_view, bool is_text)> on_message;
    std::function<void()> on_close;

private:
    asio::ip::tcp::socket socket_;
    asio::strand<asio::io_context::executor_type> strand_;
    class WebServer& server_;
    std::vector<char> buffer_;

    asio::awaitable<void> AsyncSend(std::string_view data, uint8_t opcode);
    asio::awaitable<void> AsyncClose(uint16_t code, std::string reason);

    asio::awaitable<void> ReadLoop();
    asio::awaitable<bool> ParseWsFrame(); 
};

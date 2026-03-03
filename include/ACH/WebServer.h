#pragma once

#include <asio.hpp>
#include <picohttpparser.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/bio.h>
#include <algorithm>
#include <cstring>
#include <sstream>


// ---------------------- HttpRequest ----------------------
struct HttpRequest {
    std::string_view method;
    std::string_view path;
    int minor_version = 1;
    
    std::vector<std::pair<std::string_view, std::string_view>> headers;
    std::vector<char> body;

    // 修改为大驼峰
    std::optional<std::string_view> GetHeader(std::string_view key) const;

    std::unordered_map<std::string, std::string> context;

    // 内部支持：用于确保存储在 headers 里的 string_view 有效
    std::vector<char> header_data_buffer;
};

// ---------------------- HttpResponse ----------------------
class HttpResponse {
public:
    int status = 200;
    std::string status_text = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    // 跨域相关方法 (大驼峰)
    HttpResponse& AllowAnyOrigin();
    HttpResponse& AllowOrigin(const std::string& origin);
    HttpResponse& AllowCommonCors();
    HttpResponse& AllowMethods(const std::string& methods);
    HttpResponse& AllowHeaders(const std::string& headers);
    HttpResponse& AllowCredentials(bool allow = true);
    HttpResponse& ExposeHeaders(const std::string& headers);
    HttpResponse& MaxAge(int seconds);
    
    // 基础响应方法 (大驼峰)
    HttpResponse& SetStatus(int code, std::string text = "");
    HttpResponse& SetHeader(std::string key, std::string value);
    HttpResponse& SetBody(std::string b);
    HttpResponse& Text(std::string content, std::string content_type = "text/plain");
    HttpResponse& Json(const std::string& json_str);
    
    std::string ToString() const;
};

// ---------------------- WsConnection ----------------------
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

    // 内部协程方法 (大驼峰)
    asio::awaitable<void> AsyncSend(std::string_view data, uint8_t opcode);
    asio::awaitable<void> AsyncClose(uint16_t code, std::string reason);

    asio::awaitable<void> ReadLoop();
    asio::awaitable<bool> ParseWsFrame(); 
};

// ---------------------- Handlers ----------------------
using HttpHandler = std::function<void(HttpRequest&, HttpResponse&)>;
using WsHandler   = std::function<void(std::shared_ptr<WsConnection>)>;

// ---------------------- WebServer ----------------------
class WebServer {
public:
    WebServer(asio::io_context& ioc, uint16_t port = 8080);

    void Register(std::string path, HttpHandler handler);
    void RegisterWs(std::string path, WsHandler handler);

    void Start();
    void Stop();

    asio::io_context& GetIoc() { return ioc_; }

private:
    asio::io_context& ioc_;
    asio::ip::tcp::acceptor acceptor_;
    asio::steady_timer shutdown_timer_;

    std::unordered_map<std::string, HttpHandler> http_routes_;
    std::unordered_map<std::string, WsHandler> ws_routes_;

    // 内部协程方法 (大驼峰)
    asio::awaitable<void> DoAccept();
    asio::awaitable<void> HandleConnection(asio::ip::tcp::socket socket);

    asio::awaitable<void> ProcessWsUpgrade(const HttpRequest& req, asio::ip::tcp::socket socket);
};

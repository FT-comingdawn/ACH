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
#include <algorithm>
#include <cstring>
#include <sstream>

#include "HttpRequest.h"
#include "HttpResponse.h"
#include "WsConnection.h"

class WebServer {
public:
    WebServer(asio::io_context& io_context, uint16_t port = 8080);

    void Register(std::string path, std::function<void(HttpRequest&, HttpResponse&)> handler);
    void RegisterWs(std::string path, std::function<void(std::shared_ptr<WsConnection>)> handler);

    void Start();
    void Stop();

    asio::io_context& GetIoContext() { return io_context; }

private:
    asio::io_context& io_context;
    asio::ip::tcp::acceptor acceptor_;
    asio::steady_timer shutdown_timer_;

    std::unordered_map<std::string, std::function<void(HttpRequest&, HttpResponse&)>> http_routes_;
    std::unordered_map<std::string, std::function<void(std::shared_ptr<WsConnection>)>> ws_routes_;

    // 内部协程方法 (大驼峰)
    asio::awaitable<void> DoAccept();
    asio::awaitable<void> HandleConnection(asio::ip::tcp::socket socket);

    asio::awaitable<void> ProcessWsUpgrade(const HttpRequest& req, asio::ip::tcp::socket socket);
};

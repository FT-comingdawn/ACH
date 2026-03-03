#include <ACH/WebServer.h>

// ---------------------- 辅助工具 (OpenSSL) ----------------------

static std::string Base64Encode(const unsigned char* buffer, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_write(bio, buffer, static_cast<int>(length));
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string res(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return res;
}

static std::string CalculateWsAccept(std::string_view key) {
    std::string combined = std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);
    return Base64Encode(hash, SHA_DIGEST_LENGTH);
}

// ---------------------- HttpRequest ----------------------

std::optional<std::string_view> HttpRequest::GetHeader(std::string_view key) const {
    auto IEquals = [](std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](char c1, char c2) { return tolower(static_cast<unsigned char>(c1)) == tolower(static_cast<unsigned char>(c2)); });
    };
    for (const auto& [k, v] : headers) {
        if (IEquals(k, key)) return v;
    }
    return std::nullopt;
}

// ---------------------- HttpResponse ----------------------

HttpResponse& HttpResponse::SetStatus(int code, std::string text) {
    status = code;
    if (!text.empty()) status_text = std::move(text);
    else {
        switch (code) {
            case 200: status_text = "OK"; break;
            case 101: status_text = "Switching Protocols"; break;
            case 400: status_text = "Bad Request"; break;
            case 404: status_text = "Not Found"; break;
            default: status_text = "Unknown";
        }
    }
    return *this;
}

HttpResponse& HttpResponse::SetHeader(std::string key, std::string value) {
    headers[std::move(key)] = std::move(value);
    return *this;
}

HttpResponse& HttpResponse::SetBody(std::string b) {
    body = std::move(b);
    SetHeader("Content-Length", std::to_string(body.size()));
    return *this;
}

HttpResponse& HttpResponse::Text(std::string content, std::string content_type) {
    SetHeader("Content-Type", std::move(content_type));
    return SetBody(std::move(content));
}

HttpResponse& HttpResponse::Json(const std::string& json_str) {
    return Text(json_str, "application/json");
}

std::string HttpResponse::ToString() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n";
    if (headers.find("Server") == headers.end()) oss << "Server: ACW/1.0\r\n";
    for (const auto& [k, v] : headers) oss << k << ": " << v << "\r\n";
    oss << "\r\n" << body;
    return oss.str();
}

HttpResponse& HttpResponse::AllowAnyOrigin() {
    return SetHeader("Access-Control-Allow-Origin", "*");
}

HttpResponse& HttpResponse::AllowOrigin(const std::string& origin) {
    return SetHeader("Access-Control-Allow-Origin", origin);
}

HttpResponse& HttpResponse::AllowCommonCors() {
    SetHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    SetHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    SetHeader("Access-Control-Max-Age", "86400");
    return *this;
}

HttpResponse& HttpResponse::AllowMethods(const std::string& methods) {
    return SetHeader("Access-Control-Allow-Methods", methods);
}

HttpResponse& HttpResponse::AllowHeaders(const std::string& headers) {
    return SetHeader("Access-Control-Allow-Headers", headers);
}

HttpResponse& HttpResponse::AllowCredentials(bool allow) {
    return SetHeader("Access-Control-Allow-Credentials", allow ? "true" : "false");
}

HttpResponse& HttpResponse::ExposeHeaders(const std::string& headers) {
    return SetHeader("Access-Control-Expose-Headers", headers);
}

HttpResponse& HttpResponse::MaxAge(int seconds) {
    return SetHeader("Access-Control-Max-Age", std::to_string(seconds));
}

// ---------------------- WsConnection ----------------------

WsConnection::WsConnection(asio::ip::tcp::socket socket, WebServer& server)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(server.GetIoc())),
      server_(server) {}

void WsConnection::Start() {
    co_spawn(strand_, [self = shared_from_this()]() -> asio::awaitable<void> {
        co_await self->ReadLoop();
    }, asio::detached);
}

asio::awaitable<void> WsConnection::ReadLoop() {
    try {
        std::vector<char> temp_read_buf(8192);
        for (;;) {
            auto n = co_await socket_.async_read_some(asio::buffer(temp_read_buf), asio::use_awaitable);
            buffer_.insert(buffer_.end(), temp_read_buf.begin(), temp_read_buf.begin() + n);
            while (co_await ParseWsFrame()); 
        }
    } catch (...) {
        if (on_close) on_close();
        socket_.close();
    }
}

asio::awaitable<bool> WsConnection::ParseWsFrame() {
    if (buffer_.size() < 2) co_return false;

    uint8_t b0 = static_cast<uint8_t>(buffer_[0]);
    uint8_t b1 = static_cast<uint8_t>(buffer_[1]);
    uint8_t opcode = b0 & 0x0F;
    bool mask = (b1 & 0x80) != 0;
    uint64_t payload_len = b1 & 0x7F;
    size_t header_len = 2;

    if (payload_len == 126) {
        if (buffer_.size() < 4) co_return false;
        payload_len = (static_cast<uint64_t>(static_cast<uint8_t>(buffer_[2])) << 8) | static_cast<uint8_t>(buffer_[3]);
        header_len += 2;
    } else if (payload_len == 127) {
        if (buffer_.size() < 10) co_return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | static_cast<uint8_t>(buffer_[2 + i]);
        header_len += 8;
    }

    if (mask) {
        if (buffer_.size() < header_len + 4) co_return false;
        header_len += 4;
    }

    if (buffer_.size() < header_len + payload_len) co_return false;

    std::vector<char> payload(buffer_.begin() + header_len, buffer_.begin() + header_len + payload_len);
    if (mask) {
        uint8_t mask_key[4];
        std::memcpy(mask_key, &buffer_[header_len - 4], 4);
        for (size_t i = 0; i < payload_len; ++i) payload[i] ^= mask_key[i % 4];
    }

    if (opcode == 0x1 || opcode == 0x2) {
        if (on_message) on_message({payload.data(), payload.size()}, opcode == 0x1);
    } else if (opcode == 0x8) {
        if (on_close) on_close();
        socket_.close();
        co_return false;
    }

    buffer_.erase(buffer_.begin(), buffer_.begin() + header_len + payload_len);
    co_return true;
}

asio::awaitable<void> WsConnection::AsyncSend(std::string_view data, uint8_t opcode) {
    std::vector<char> frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    if (data.size() < 126) {
        frame.push_back(static_cast<char>(data.size()));
    } else if (data.size() <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((data.size() >> 8) & 0xFF));
        frame.push_back(static_cast<char>(data.size() & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) frame.push_back(static_cast<char>((data.size() >> (i * 8)) & 0xFF));
    }
    frame.insert(frame.end(), data.begin(), data.end());
    co_await asio::async_write(socket_, asio::buffer(frame), asio::use_awaitable);
}

void WsConnection::SendText(std::string text) {
    co_spawn(strand_, [self = shared_from_this(), t = std::move(text)]() -> asio::awaitable<void> {
        co_await self->AsyncSend(t, 0x01);
    }, asio::detached);
}

void WsConnection::SendBinary(std::span<const char> data) {
    co_spawn(strand_, [self = shared_from_this(), d = std::vector<char>(data.begin(), data.end())]() -> asio::awaitable<void> {
        co_await self->AsyncSend({d.data(), d.size()}, 0x02);
    }, asio::detached);
}

void WsConnection::Close(uint16_t code, std::string reason) {
    co_spawn(strand_, [self = shared_from_this(), code, r = std::move(reason)]() -> asio::awaitable<void> {
        std::string p; 
        p.push_back(static_cast<char>((code >> 8) & 0xFF)); 
        p.push_back(static_cast<char>(code & 0xFF)); 
        p += r;
        try { co_await self->AsyncSend(p, 0x08); } catch(...) {}
        self->socket_.close();
        if (self->on_close) self->on_close();
    }, asio::detached);
}

// ---------------------- WebServer ----------------------

WebServer::WebServer(asio::io_context& ioc, uint16_t port)
    : ioc_(ioc), acceptor_(ioc, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)), shutdown_timer_(ioc) {}

void WebServer::Register(std::string path, HttpHandler handler) { http_routes_[path] = std::move(handler); }
void WebServer::RegisterWs(std::string path, WsHandler handler) { ws_routes_[path] = std::move(handler); }
void WebServer::Start() { co_spawn(ioc_, DoAccept(), asio::detached); }
void WebServer::Stop() { acceptor_.close(); }

asio::awaitable<void> WebServer::DoAccept() {
    for (;;) {
        auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
        co_spawn(ioc_, [this, s = std::move(socket)]() mutable -> asio::awaitable<void> {
            co_await HandleConnection(std::move(s));
        }, asio::detached);
    }
}

asio::awaitable<void> WebServer::HandleConnection(asio::ip::tcp::socket socket) {
    std::vector<char> buf(16384);
    size_t offset = 0;
    try {
        for (;;) {
            int ret = -2;
            const char *method, *path; size_t m_len, p_len; int minor;
            struct phr_header ph_headers[100]; size_t num_headers = 100;

            while (ret == -2) {
                auto n = co_await socket.async_read_some(asio::buffer(buf.data() + offset, buf.size() - offset), asio::use_awaitable);
                offset += n;
                ret = phr_parse_request(buf.data(), offset, &method, &m_len, &path, &p_len, &minor, ph_headers, &num_headers, offset - n);
            }
            if (ret == -1) co_return;

            HttpRequest req;
            req.header_data_buffer.assign(buf.begin(), buf.begin() + ret);
            req.method = std::string_view(req.header_data_buffer.data() + (method - buf.data()), m_len);
            req.path = std::string_view(req.header_data_buffer.data() + (path - buf.data()), p_len);
            req.minor_version = minor;
            for (size_t i = 0; i < num_headers; ++i) {
                req.headers.emplace_back(
                    std::string_view(req.header_data_buffer.data() + (ph_headers[i].name - buf.data()), ph_headers[i].name_len),
                    std::string_view(req.header_data_buffer.data() + (ph_headers[i].value - buf.data()), ph_headers[i].value_len)
                );
            }

            auto cl_hdr = req.GetHeader("Content-Length");
            if (cl_hdr) {
                size_t content_len = std::stoull(std::string(*cl_hdr));
                req.body.resize(content_len);
                size_t body_in_buf = offset - ret;
                if (body_in_buf > 0) std::memcpy(req.body.data(), buf.data() + ret, std::min(body_in_buf, content_len));
                if (body_in_buf < content_len) {
                    co_await asio::async_read(socket, asio::buffer(req.body.data() + body_in_buf, content_len - body_in_buf), asio::use_awaitable);
                }
            }
            offset = 0; 

            if (auto upgrade = req.GetHeader("Upgrade"); upgrade && *upgrade == "websocket") {
                co_await ProcessWsUpgrade(req, std::move(socket));
                co_return;
            }

            HttpResponse res;
            if (auto it = http_routes_.find(std::string(req.path)); it != http_routes_.end()) it->second(req, res);
            else res.SetStatus(404).Text("Not Found");

            std::string reply = res.ToString();
            co_await asio::async_write(socket, asio::buffer(reply), asio::use_awaitable);
            if (req.GetHeader("Connection") == "close") break;
        }
    } catch(...) {}
}

asio::awaitable<void> WebServer::ProcessWsUpgrade(const HttpRequest& req, asio::ip::tcp::socket socket) {
    auto key = req.GetHeader("Sec-WebSocket-Key");
    if (!key) co_return;

    HttpResponse res;
    res.SetStatus(101).SetHeader("Upgrade", "websocket").SetHeader("Connection", "Upgrade")
       .SetHeader("Sec-WebSocket-Accept", CalculateWsAccept(*key));

    co_await asio::async_write(socket, asio::buffer(res.ToString()), asio::use_awaitable);
    auto conn = std::make_shared<WsConnection>(std::move(socket), *this);
    if (auto it = ws_routes_.find(std::string(req.path)); it != ws_routes_.end()) it->second(conn);
    conn->Start();
}

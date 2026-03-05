#include "ACH/WebServer.h"

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

WebServer::WebServer(asio::io_context& io_context, uint16_t port)
    : io_context(io_context), acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)), shutdown_timer_(io_context) {}

void WebServer::Register(std::string path, std::function<void(HttpRequest&, HttpResponse&)> handler) { http_routes_[path] = std::move(handler); }
void WebServer::RegisterWs(std::string path, std::function<void(std::shared_ptr<WsConnection>)> handler) { ws_routes_[path] = std::move(handler); }
void WebServer::Start() { co_spawn(io_context, DoAccept(), asio::detached); }
void WebServer::Stop() { acceptor_.close(); }

asio::awaitable<void> WebServer::DoAccept() {
    for (;;) {
        auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
        co_spawn(io_context, [this, s = std::move(socket)]() mutable -> asio::awaitable<void> {
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

#include "ACH/WebServer.h"
#include "ACH/WsConnection.h"

WsConnection::WsConnection(asio::ip::tcp::socket socket, WebServer& server)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(server.GetIoContext())),
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

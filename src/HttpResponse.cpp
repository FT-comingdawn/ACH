#include "ACH/HttpResponse.h"

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

#pragma once
#include <string>
#include <sstream>
#include <unordered_map>

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

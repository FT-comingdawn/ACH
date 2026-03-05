#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

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

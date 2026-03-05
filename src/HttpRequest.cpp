#include "ACH/HttpRequest.h"

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

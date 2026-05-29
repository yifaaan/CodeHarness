#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::appserver {

struct ServerConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{8080};
    std::string auth_token;
    std::vector<std::string> cors_origins;
};

auto run(const ServerConfig& config) -> int;

} // namespace codeharness::appserver

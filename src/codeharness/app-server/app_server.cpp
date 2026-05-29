#include <print>
#include "app_server.hpp"

namespace codeharness::appserver {

auto run(const ServerConfig& config) -> int {
    std::println("CodeHarness App Server starting on {}:{}", config.host, config.port);
    return 0;
}

} // namespace codeharness::appserver

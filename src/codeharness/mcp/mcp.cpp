#include "mcp.hpp"

namespace codeharness::mcp {

auto McpManager::register_server(const McpServerConfig& config) -> bool {
    servers_.push_back(config);
    return true;
}

auto McpManager::start_all() -> bool {
    return true;
}

auto McpManager::stop_all() -> bool {
    return true;
}

auto McpManager::call_tool(std::string_view server, std::string_view tool, std::string_view args) -> std::string {
    return {};
}

} // namespace codeharness::mcp

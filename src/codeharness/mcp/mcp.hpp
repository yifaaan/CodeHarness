#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::mcp {

struct McpServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::vector<std::string> allowed_tools;
};

class McpManager {
public:
    auto register_server(const McpServerConfig& config) -> bool;
    auto start_all() -> bool;
    auto stop_all() -> bool;
    auto call_tool(std::string_view server, std::string_view tool, std::string_view args) -> std::string;

private:
    std::vector<McpServerConfig> servers_;
};

} // namespace codeharness::mcp

#include "tools.hpp"

namespace codeharness::tools {

auto ToolRegistry::execute_tool(const ToolCall& call) -> ToolResult {
    return ToolResult{};
}

auto ToolRegistry::list_tools() const -> std::vector<std::string> {
    return {};
}

} // namespace codeharness::tools

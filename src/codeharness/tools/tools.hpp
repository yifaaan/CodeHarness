#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tools {

struct ToolCall {
    std::string name;
    std::string arguments;
    std::string id;
};

struct ToolResult {
    bool success{false};
    std::string output;
    std::string error;
};

enum class ToolErrorKind {
    InvalidInput,
    MissingField,
    PathEscape,
    ExecutionFailed,
    Timeout,
    NotAvailable,
    PermissionDenied
};

class ToolRegistry {
public:
    auto execute_tool(const ToolCall& call) -> ToolResult;
    auto list_tools() const -> std::vector<std::string>;
};

} // namespace codeharness::tools

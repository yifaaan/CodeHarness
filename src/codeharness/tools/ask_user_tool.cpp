#include "codeharness/tools/ask_user_tool.h"

namespace codeharness
{

auto AskUserTool::name() const -> std::string
{
    return "ask_user";
}

auto AskUserTool::description() const -> std::string
{
    return "Ask the user a question when more input is needed. Input: {\"question\": string, \"reason\"?: string}.";
}

auto AskUserTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto AskUserTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    return ToolResponse{
        .tool_use_id = request.id,
        .content = "user input unavailable in non-interactive mode",
        .is_error = true,
    };
}

} // namespace codeharness

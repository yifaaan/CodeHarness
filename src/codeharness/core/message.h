#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace codeharness
{

enum class Role
{
    System,
    User,
    Assistant,
    Tool
};

struct TextBlock
{
    std::string text;
};

struct ToolUseBlock
{
    std::string id;
    std::string name;
    std::string input_json;
};

struct ToolResultBlock
{
    std::string tool_use_id;
    std::string content;
    bool is_error = false;
};

using ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock>;

} // namespace codeharness
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

struct Message
{
    Role role = Role::User;
    std::vector<ContentBlock> content;
};

inline auto make_text_message(Role role, std::string text) -> Message
{
    Message message;
    message.role = role;
    message.content.emplace_back(TextBlock{std::move(text)});
    return message;
}

inline auto collect_text(const Message& message) -> std::string
{
    std::string result;
    for (const auto& block : message.content)
    {
        if (auto text_block = std::get_if<TextBlock>(&block))
        {
            result += text_block->text;
        }
    }
    return result;
}

} // namespace codeharness
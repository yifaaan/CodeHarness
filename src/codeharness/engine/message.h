#pragma once

#include <absl/strings/string_view.h>

#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace codeharness::engine {
    struct TextBlock {
        std::string text;
    };
    struct ToolUseBlock {
        std::string id;
        std::string name;
        nlohmann::json input;
    };
    struct ToolResultBlock {
        std::string tool_use_id;
        std::string content;
        bool is_error{};
    };

    using ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock>;

    enum class MessageRole {
        user,
        assistant,
    };
    // A single assistant or user message.
    struct ConversationMessage {
        MessageRole role{MessageRole::user};
        std::vector<ContentBlock> content;

        // Construct a user message from raw text.
        static auto from_user_text(std::string text) {
            return ConversationMessage{
                .role = MessageRole::user,
                .content = {TextBlock{.text = std::move(text)}},
            };
        }

        // Return concatenated text blocks.
        auto text() const {
            std::string result;
            for (const auto& block : content) {
                if (auto text = std::get_if<TextBlock>(&block)) {
                    result += text->text;
                }
            }
            return result;
        }

        // Return all tool calls contained in the message.
        auto tool_uses() const {
            std::vector<ToolUseBlock> result;
            for (const auto& block : content) {
                if (auto tool = std::get_if<ToolUseBlock>(&block)) {
                    result.push_back(*tool);
                }
            }
            return result;
        }
    };
}  // namespace codeharness::engine

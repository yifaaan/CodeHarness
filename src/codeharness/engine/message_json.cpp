#include "codeharness/engine/message_json.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace codeharness::engine {
namespace {

    [[nodiscard]] auto role_to_string(MessageRole role) -> const char* {
        switch (role) {
            case MessageRole::user:
                return "user";
            case MessageRole::assistent:
                return "assistant";
        }

        throw std::invalid_argument{"unknown message role"};
    }

    [[nodiscard]] auto role_from_string(const std::string& role) -> MessageRole {
        if (role == "user") {
            return MessageRole::user;
        }

        if (role == "assistant" || role == "assistent") {
            return MessageRole::assistent;
        }

        throw std::invalid_argument{"unknown message role: " + role};
    }

    [[nodiscard]] auto require_type(const nlohmann::json& value) -> std::string {
        if (!value.is_object()) {
            throw std::invalid_argument{"content block must be a JSON object"};
        }

        return value.at("type").get<std::string>();
    }

}  // namespace

    auto to_json(const TextBlock& block) -> nlohmann::json {
        return {
            {"type", "text"},
            {"text", block.text},
        };
    }

    auto to_json(const ToolUseBlock& block) -> nlohmann::json {
        return {
            {"type", "tool_use"},
            {"id", block.id},
            {"name", block.name},
            {"input", block.input},
        };
    }

    auto to_json(const ToolResultBlock& block) -> nlohmann::json {
        return {
            {"type", "tool_result"},
            {"tool_use_id", block.tool_use_id},
            {"content", block.content},
            {"is_error", block.is_error},
        };
    }

    auto to_json(const ContentBlock& block) -> nlohmann::json {
        return std::visit([](const auto& item) { return to_json(item); }, block);
    }

    auto to_json(const ConversationMessage& message) -> nlohmann::json {
        auto content = nlohmann::json::array();
        for (const auto& block : message.content) {
            content.push_back(to_json(block));
        }

        return {
            {"role", role_to_string(message.role)},
            {"content", std::move(content)},
        };
    }

    auto content_block_from_json(const nlohmann::json& value) -> ContentBlock {
        const auto type = require_type(value);

        if (type == "text") {
            return TextBlock{
                .text = value.at("text").get<std::string>(),
            };
        }

        if (type == "tool_use") {
            return ToolUseBlock{
                .id = value.at("id").get<std::string>(),
                .name = value.at("name").get<std::string>(),
                .input = value.value("input", nlohmann::json::object()),
            };
        }

        if (type == "tool_result") {
            return ToolResultBlock{
                .tool_use_id = value.at("tool_use_id").get<std::string>(),
                .content = value.at("content").get<std::string>(),
                .is_error = value.value("is_error", false),
            };
        }

        throw std::invalid_argument{"unknown content block type: " + type};
    }

    auto conversation_message_from_json(const nlohmann::json& value) -> ConversationMessage {
        if (!value.is_object()) {
            throw std::invalid_argument{"conversation message must be a JSON object"};
        }

        auto content = std::vector<ContentBlock>{};
        for (const auto& block : value.at("content")) {
            content.push_back(content_block_from_json(block));
        }

        return ConversationMessage{
            .role = role_from_string(value.at("role").get<std::string>()),
            .content = std::move(content),
        };
    }

}  // namespace codeharness::engine

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

    auto to_json(nlohmann::json& value, const TextBlock& block) -> void {
        value = {
            {"type", "text"},
            {"text", block.text},
        };
    }

    auto to_json(nlohmann::json& value, const ToolUseBlock& block) -> void {
        value = {
            {"type", "tool_use"},
            {"id", block.id},
            {"name", block.name},
            {"input", block.input},
        };
    }

    auto to_json(nlohmann::json& value, const ToolResultBlock& block) -> void {
        value = {
            {"type", "tool_result"},
            {"tool_use_id", block.tool_use_id},
            {"content", block.content},
            {"is_error", block.is_error},
        };
    }

    auto to_json(nlohmann::json& value, const ContentBlock& block) -> void {
        std::visit([&value](const auto& item) { to_json(value, item); }, block);
    }

    auto to_json(nlohmann::json& value, const ConversationMessage& message) -> void {
        auto content = nlohmann::json::array();
        for (const auto& block : message.content) {
            nlohmann::json serialized_block;
            to_json(serialized_block, block);
            content.push_back(std::move(serialized_block));
        }

        value = {
            {"role", role_to_string(message.role)},
            {"content", std::move(content)},
        };
    }

    auto from_json(const nlohmann::json& value, TextBlock& block) -> void {
        if (require_type(value) != "text") {
            throw std::invalid_argument{"content block is not text"};
        }

        block = TextBlock{
            .text = value.at("text").get<std::string>(),
        };
    }

    auto from_json(const nlohmann::json& value, ToolUseBlock& block) -> void {
        if (require_type(value) != "tool_use") {
            throw std::invalid_argument{"content block is not tool_use"};
        }

        block = ToolUseBlock{
            .id = value.at("id").get<std::string>(),
            .name = value.at("name").get<std::string>(),
            .input = value.value("input", nlohmann::json::object()),
        };
    }

    auto from_json(const nlohmann::json& value, ToolResultBlock& block) -> void {
        if (require_type(value) != "tool_result") {
            throw std::invalid_argument{"content block is not tool_result"};
        }

        block = ToolResultBlock{
            .tool_use_id = value.at("tool_use_id").get<std::string>(),
            .content = value.at("content").get<std::string>(),
            .is_error = value.value("is_error", false),
        };
    }

    auto content_block_from_json(const nlohmann::json& value) -> ContentBlock {
        const auto type = require_type(value);

        if (type == "text") {
            auto block = TextBlock{};
            from_json(value, block);
            return block;
        }

        if (type == "tool_use") {
            auto block = ToolUseBlock{};
            from_json(value, block);
            return block;
        }

        if (type == "tool_result") {
            auto block = ToolResultBlock{};
            from_json(value, block);
            return block;
        }

        throw std::invalid_argument{"unknown content block type: " + type};
    }

    auto from_json(const nlohmann::json& value, ContentBlock& block) -> void {
        block = content_block_from_json(value);
    }

    auto from_json(const nlohmann::json& value, ConversationMessage& message) -> void {
        if (!value.is_object()) {
            throw std::invalid_argument{"conversation message must be a JSON object"};
        }

        auto content = std::vector<ContentBlock>{};
        for (const auto& block : value.at("content")) {
            content.push_back(content_block_from_json(block));
        }

        message = ConversationMessage{
            .role = role_from_string(value.at("role").get<std::string>()),
            .content = std::move(content),
        };
    }

    auto to_json(const ConversationMessage& message) -> nlohmann::json {
        nlohmann::json value;
        to_json(value, message);
        return value;
    }

    auto conversation_message_from_json(const nlohmann::json& value) -> ConversationMessage {
        ConversationMessage message;
        from_json(value, message);
        return message;
    }

}  // namespace codeharness::engine

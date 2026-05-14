#include "codeharness/engine/message_json.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <string>
#include <utility>

namespace codeharness::engine {
namespace {

    [[nodiscard]] auto role_to_string(MessageRole role) -> const char* {
        switch (role) {
            case MessageRole::user:
                return "user";
            case MessageRole::assistant:
                return "assistant";
        }

        return "assistant";
    }

    [[nodiscard]] auto role_from_string(const std::string& role) -> absl::StatusOr<MessageRole> {
        if (role == "user") {
            return MessageRole::user;
        }
        if (role == "assistant") {
            return MessageRole::assistant;
        }

        return absl::InvalidArgumentError("unknown message role: " + role);
    }

    [[nodiscard]] auto require_type(const nlohmann::json& value) -> absl::StatusOr<std::string> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("content block must be a JSON object");
        }
        if (!value.contains("type") || !value.at("type").is_string()) {
            return absl::InvalidArgumentError("content block is missing string field: type");
        }

        return value.at("type").get<std::string>();
    }

    // Field validation only; caller has already matched JSON "type".
    [[nodiscard]] auto text_block_payload(const nlohmann::json& value) -> absl::StatusOr<TextBlock> {
        if (!value.contains("text") || !value.at("text").is_string()) {
            return absl::InvalidArgumentError("text block is missing string field: text");
        }

        return TextBlock{
            .text = value.at("text").get<std::string>(),
        };
    }

    [[nodiscard]] auto tool_use_block_payload(const nlohmann::json& value)
        -> absl::StatusOr<ToolUseBlock> {
        if (!value.contains("id") || !value.at("id").is_string()) {
            return absl::InvalidArgumentError("tool_use block is missing string field: id");
        }
        if (!value.contains("name") || !value.at("name").is_string()) {
            return absl::InvalidArgumentError("tool_use block is missing string field: name");
        }

        return ToolUseBlock{
            .id = value.at("id").get<std::string>(),
            .name = value.at("name").get<std::string>(),
            .input = value.value("input", nlohmann::json::object()),
        };
    }

    [[nodiscard]] auto tool_result_block_payload(const nlohmann::json& value)
        -> absl::StatusOr<ToolResultBlock> {
        if (!value.contains("tool_use_id") || !value.at("tool_use_id").is_string()) {
            return absl::InvalidArgumentError(
                "tool_result block is missing string field: tool_use_id");
        }
        if (!value.contains("content") || !value.at("content").is_string()) {
            return absl::InvalidArgumentError("tool_result block is missing string field: content");
        }

        return ToolResultBlock{
            .tool_use_id = value.at("tool_use_id").get<std::string>(),
            .content = value.at("content").get<std::string>(),
            .is_error = value.value("is_error", false),
        };
    }

    [[nodiscard]] auto text_block_from_json(const nlohmann::json& value)
        -> absl::StatusOr<TextBlock> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "text") {
            return absl::InvalidArgumentError("content block is not text");
        }
        return text_block_payload(value);
    }

    [[nodiscard]] auto tool_use_block_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ToolUseBlock> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "tool_use") {
            return absl::InvalidArgumentError("content block is not tool_use");
        }
        return tool_use_block_payload(value);
    }

    [[nodiscard]] auto tool_result_block_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ToolResultBlock> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "tool_result") {
            return absl::InvalidArgumentError("content block is not tool_result");
        }
        return tool_result_block_payload(value);
    }

    [[nodiscard]] auto content_block_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ContentBlock> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }

        if (*type == "text") {
            const auto block = text_block_payload(value);
            if (!block.ok()) {
                return block.status();
            }
            return ContentBlock{std::move(*block)};
        }

        if (*type == "tool_use") {
            const auto block = tool_use_block_payload(value);
            if (!block.ok()) {
                return block.status();
            }
            return ContentBlock{std::move(*block)};
        }

        if (*type == "tool_result") {
            const auto block = tool_result_block_payload(value);
            if (!block.ok()) {
                return block.status();
            }
            return ContentBlock{std::move(*block)};
        }

        return absl::InvalidArgumentError("unknown content block type: " + *type);
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
        const auto parsed = text_block_from_json(value);
        block = parsed.ok() ? std::move(*parsed) : TextBlock{};
    }

    auto from_json(const nlohmann::json& value, ToolUseBlock& block) -> void {
        const auto parsed = tool_use_block_from_json(value);
        block = parsed.ok() ? std::move(*parsed) : ToolUseBlock{};
    }

    auto from_json(const nlohmann::json& value, ToolResultBlock& block) -> void {
        const auto parsed = tool_result_block_from_json(value);
        block = parsed.ok() ? std::move(*parsed) : ToolResultBlock{};
    }

    auto from_json(const nlohmann::json& value, ContentBlock& block) -> void {
        const auto parsed = content_block_from_json(value);
        block = parsed.ok() ? std::move(*parsed) : ContentBlock{TextBlock{}};
    }

    auto from_json(const nlohmann::json& value, ConversationMessage& message) -> void {
        const auto parsed = conversation_message_from_json(value);
        message = parsed.ok() ? std::move(*parsed) : ConversationMessage{};
    }

    auto conversation_message_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ConversationMessage> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("conversation message must be a JSON object");
        }
        if (!value.contains("role") || !value.at("role").is_string()) {
            return absl::InvalidArgumentError("conversation message is missing string field: role");
        }
        if (!value.contains("content") || !value.at("content").is_array()) {
            return absl::InvalidArgumentError("conversation message is missing content array");
        }

        const auto role = role_from_string(value.at("role").get<std::string>());
        if (!role.ok()) {
            return role.status();
        }

        auto content = std::vector<ContentBlock>{};
        content.reserve(value.at("content").size());
        for (const auto& block : value.at("content")) {
            const auto parsed_block = content_block_from_json(block);
            if (!parsed_block.ok()) {
                return parsed_block.status();
            }
            content.push_back(std::move(*parsed_block));
        }

        return ConversationMessage{
            .role = *role,
            .content = std::move(content),
        };
    }

}  // namespace codeharness::engine

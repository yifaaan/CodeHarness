#include "codeharness/engine/message_json.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <string>
#include <utility>

namespace codeharness::engine {
    namespace {

        [[nodiscard]] auto content_block_from_json(const nlohmann::json& value)
            -> absl::StatusOr<ContentBlock> {
            if (!value.is_object()) {
                return absl::InvalidArgumentError("content block must be a JSON object");
            }
            if (!value.contains("type") || !value.at("type").is_string()) {
                return absl::InvalidArgumentError("content block is missing string field: type");
            }

            const auto type = value.at("type").get<std::string>();

            if (type == "text") {
                if (!value.contains("text") || !value.at("text").is_string()) {
                    return absl::InvalidArgumentError("text block is missing string field: text");
                }

                return ContentBlock{
                    TextBlock{
                        .text = value.at("text").get<std::string>(),
                    },
                };
            }

            if (type == "tool_use") {
                if (!value.contains("id") || !value.at("id").is_string()) {
                    return absl::InvalidArgumentError(
                        "tool_use block is missing string field: id");
                }
                if (!value.contains("name") || !value.at("name").is_string()) {
                    return absl::InvalidArgumentError(
                        "tool_use block is missing string field: name");
                }

                return ContentBlock{
                    ToolUseBlock{
                        .id = value.at("id").get<std::string>(),
                        .name = value.at("name").get<std::string>(),
                        .input = value.value("input", nlohmann::json::object()),
                    },
                };
            }

            if (type == "tool_result") {
                if (!value.contains("tool_use_id") || !value.at("tool_use_id").is_string()) {
                    return absl::InvalidArgumentError(
                        "tool_result block is missing string field: tool_use_id");
                }
                if (!value.contains("content") || !value.at("content").is_string()) {
                    return absl::InvalidArgumentError(
                        "tool_result block is missing string field: content");
                }

                return ContentBlock{
                    ToolResultBlock{
                        .tool_use_id = value.at("tool_use_id").get<std::string>(),
                        .content = value.at("content").get<std::string>(),
                        .is_error = value.value("is_error", false),
                    },
                };
            }

            return absl::InvalidArgumentError("unknown content block type: " + type);
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
            {"role", message.role == MessageRole::user ? "user" : "assistant"},
            {"content", std::move(content)},
        };
    }

    auto from_json(const nlohmann::json& value, TextBlock& block) -> void {
        const auto parsed = content_block_from_json(value);
        if (!parsed.ok()) {
            block = TextBlock{};
            return;
        }

        const auto* item = std::get_if<TextBlock>(&*parsed);
        block = item != nullptr ? *item : TextBlock{};
    }

    auto from_json(const nlohmann::json& value, ToolUseBlock& block) -> void {
        const auto parsed = content_block_from_json(value);
        if (!parsed.ok()) {
            block = ToolUseBlock{};
            return;
        }

        const auto* item = std::get_if<ToolUseBlock>(&*parsed);
        block = item != nullptr ? *item : ToolUseBlock{};
    }

    auto from_json(const nlohmann::json& value, ToolResultBlock& block) -> void {
        const auto parsed = content_block_from_json(value);
        if (!parsed.ok()) {
            block = ToolResultBlock{};
            return;
        }

        const auto* item = std::get_if<ToolResultBlock>(&*parsed);
        block = item != nullptr ? *item : ToolResultBlock{};
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

        const auto role_str = value.at("role").get<std::string>();
        auto role = MessageRole::assistant;
        if (role_str == "user") {
            role = MessageRole::user;
        } else if (role_str != "assistant") {
            return absl::InvalidArgumentError("unknown message role: " + role_str);
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
            .role = role,
            .content = std::move(content),
        };
    }

}  // namespace codeharness::engine

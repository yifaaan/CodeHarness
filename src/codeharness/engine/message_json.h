#pragma once

#include <absl/status/statusor.h>
#include <nlohmann/json.hpp>

#include "codeharness/engine/message.h"

namespace codeharness::engine {
    auto to_json(nlohmann::json& value, const TextBlock& block) -> void;
    auto to_json(nlohmann::json& value, const ToolUseBlock& block) -> void;
    auto to_json(nlohmann::json& value, const ToolResultBlock& block) -> void;
    auto to_json(nlohmann::json& value, const ContentBlock& block) -> void;
    auto to_json(nlohmann::json& value, const ConversationMessage& message) -> void;

    auto from_json(const nlohmann::json& value, TextBlock& block) -> void;
    auto from_json(const nlohmann::json& value, ToolUseBlock& block) -> void;
    auto from_json(const nlohmann::json& value, ToolResultBlock& block) -> void;
    auto from_json(const nlohmann::json& value, ContentBlock& block) -> void;
    auto from_json(const nlohmann::json& value, ConversationMessage& message) -> void;

    [[nodiscard]] auto to_json(const ConversationMessage& message) -> nlohmann::json;
    [[nodiscard]] auto conversation_message_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ConversationMessage>;

}  // namespace codeharness::engine

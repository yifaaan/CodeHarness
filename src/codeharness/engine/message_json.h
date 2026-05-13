#pragma once

#include <nlohmann/json.hpp>

#include "codeharness/engine/message.h"

namespace codeharness::engine {

    [[nodiscard]] auto to_json(const TextBlock& block) -> nlohmann::json;
    [[nodiscard]] auto to_json(const ToolUseBlock& block) -> nlohmann::json;
    [[nodiscard]] auto to_json(const ToolResultBlock& block) -> nlohmann::json;
    [[nodiscard]] auto to_json(const ContentBlock& block) -> nlohmann::json;
    [[nodiscard]] auto to_json(const ConversationMessage& message) -> nlohmann::json;

    [[nodiscard]] auto content_block_from_json(const nlohmann::json& value) -> ContentBlock;
    [[nodiscard]] auto conversation_message_from_json(const nlohmann::json& value)
        -> ConversationMessage;

}  // namespace codeharness::engine

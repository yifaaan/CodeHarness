#pragma once

#include <nlohmann/json.hpp>

#include "codeharness/engine/message.h"

namespace codeharness::engine {
    [[nodiscard]] auto to_json(const ConversationMessage& message) -> nlohmann::json;
    [[nodiscard]] auto conversation_message_from_json(const nlohmann::json& value)
        -> ConversationMessage;

}  // namespace codeharness::engine

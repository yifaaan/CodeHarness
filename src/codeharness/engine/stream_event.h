#pragma once

#include <nlohmann/json.hpp>
#include <variant>

#include "codeharness/engine/message_json.h"

namespace codeharness::engine {
    struct UsageSnapshot {
        int input_tokens{0};
        int output_tokens{0};
    };

    // Incremental assistant text.
    struct AssistantTextDelta {
        std::string text;
    };

    // Completed assistant turn.
    struct AssistantTurnComplete {
        ConversationMessage message;
        UsageSnapshot usage;
    };

    // The engine is about to execute a tool.
    struct ToolExecutionStared {
        std::string tool_name;
        nlohmann::json tool_input;
    };

    // A tool has finished executing
    struct ToolExecutionComplete {
        std::string tool_name;
        std::string output;
        bool is_error{};
    };

    // query_engine exposed UI events
    using StreamEvent = std::variant<AssistantTextDelta, AssistantTurnComplete, ToolExecutionStared,
                                     ToolExecutionComplete>;

    auto to_json(nlohmann::json& value, const UsageSnapshot& usage) -> void;
    auto from_json(const nlohmann::json& value, UsageSnapshot& usage) -> void;
    auto to_json(nlohmann::json& value, const AssistantTextDelta& delta) -> void;
    auto from_json(const nlohmann::json& value, AssistantTextDelta& delta) -> void;
    auto to_json(nlohmann::json& value, const AssistantTurnComplete& complete) -> void;
    auto from_json(const nlohmann::json& value, AssistantTurnComplete& complete) -> void;
    auto to_json(nlohmann::json& value, const ToolExecutionStared& started) -> void;
    auto from_json(const nlohmann::json& value, ToolExecutionStared& started) -> void;
    auto to_json(nlohmann::json& value, const ToolExecutionComplete& complete) -> void;
    auto from_json(const nlohmann::json& value, ToolExecutionComplete& complete) -> void;
    auto to_json(nlohmann::json& value, const StreamEvent& event) -> void;
    auto from_json(const nlohmann::json& value, StreamEvent& event) -> void;
}  // namespace codeharness::engine

#pragma once

#include <nlohmann/json.hpp>
#include <variant>

#include "message.h"

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

    using StreamEvent = std::variant<AssistantTextDelta, AssistantTurnComplete, ToolExecutionStared,
                                     ToolExecutionComplete>;
}  // namespace codeharness::engine
#include "codeharness/ui/stream_json_renderer.h"

namespace codeharness::ui {
    auto to_stream_json(const engine::StreamEvent& event) -> nlohmann::json {
        if (const auto* delta = std::get_if<engine::AssistantTextDelta>(&event)) {
            return {
                {"type", "assistant_delta"},
                {"text", delta->text},
            };
        }

        if (const auto* complete = std::get_if<engine::AssistantTurnComplete>(&event)) {
            return {
                {"type", "assistant_complete"},
                {"text", complete->message.text()},
                {"usage",
                 {
                     {"input_tokens", complete->usage.input_tokens},
                     {"output_tokens", complete->usage.output_tokens},
                 }},
            };
        }

        if (const auto* started = std::get_if<engine::ToolExecutionStared>(&event)) {
            return {
                {"type", "tool_start"},
                {"tool_name", started->tool_name},
                {"tool_input", started->tool_input},
            };
        }

        if (const auto* completed = std::get_if<engine::ToolExecutionComplete>(&event)) {
            return {
                {"type", "tool_complete"},
                {"tool_name", completed->tool_name},
                {"output", completed->output},
                {"is_error", completed->is_error},
            };
        }

        return {
            {"type", "unknown"},
        };
    }

}  // namespace codeharness::ui
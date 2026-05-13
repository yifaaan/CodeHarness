#include "codeharness/engine/stream_event.h"

#include <stdexcept>
#include <string>

namespace codeharness::engine {
namespace {

    [[nodiscard]] auto require_type(const nlohmann::json& value) -> std::string {
        if (!value.is_object()) {
            throw std::invalid_argument{"stream event must be a JSON object"};
        }

        return value.at("type").get<std::string>();
    }

}  // namespace

    auto to_json(nlohmann::json& value, const UsageSnapshot& usage) -> void {
        value = {
            {"input_tokens", usage.input_tokens},
            {"output_tokens", usage.output_tokens},
        };
    }

    auto from_json(const nlohmann::json& value, UsageSnapshot& usage) -> void {
        usage = UsageSnapshot{
            .input_tokens = value.value("input_tokens", 0),
            .output_tokens = value.value("output_tokens", 0),
        };
    }

    auto to_json(nlohmann::json& value, const AssistantTextDelta& delta) -> void {
        value = {
            {"type", "assistant_delta"},
            {"text", delta.text},
        };
    }

    auto from_json(const nlohmann::json& value, AssistantTextDelta& delta) -> void {
        if (require_type(value) != "assistant_delta") {
            throw std::invalid_argument{"stream event is not assistant_delta"};
        }

        delta = AssistantTextDelta{
            .text = value.value("text", ""),
        };
    }

    auto to_json(nlohmann::json& value, const AssistantTurnComplete& complete) -> void {
        value = {
            {"type", "assistant_complete"},
            {"text", complete.message.text()},
            {"message", complete.message},
            {"usage", complete.usage},
        };
    }

    auto from_json(const nlohmann::json& value, AssistantTurnComplete& complete) -> void {
        if (require_type(value) != "assistant_complete") {
            throw std::invalid_argument{"stream event is not assistant_complete"};
        }

        complete = AssistantTurnComplete{
            .message = value.at("message").get<ConversationMessage>(),
            .usage = value.value("usage", nlohmann::json::object()).get<UsageSnapshot>(),
        };
    }

    auto to_json(nlohmann::json& value, const ToolExecutionStared& started) -> void {
        value = {
            {"type", "tool_start"},
            {"tool_name", started.tool_name},
            {"tool_input", started.tool_input},
        };
    }

    auto from_json(const nlohmann::json& value, ToolExecutionStared& started) -> void {
        if (require_type(value) != "tool_start") {
            throw std::invalid_argument{"stream event is not tool_start"};
        }

        started = ToolExecutionStared{
            .tool_name = value.value("tool_name", ""),
            .tool_input = value.value("tool_input", nlohmann::json::object()),
        };
    }

    auto to_json(nlohmann::json& value, const ToolExecutionComplete& complete) -> void {
        value = {
            {"type", "tool_complete"},
            {"tool_name", complete.tool_name},
            {"output", complete.output},
            {"is_error", complete.is_error},
        };
    }

    auto from_json(const nlohmann::json& value, ToolExecutionComplete& complete) -> void {
        if (require_type(value) != "tool_complete") {
            throw std::invalid_argument{"stream event is not tool_complete"};
        }

        complete = ToolExecutionComplete{
            .tool_name = value.value("tool_name", ""),
            .output = value.value("output", ""),
            .is_error = value.value("is_error", false),
        };
    }

    auto to_json(nlohmann::json& value, const StreamEvent& event) -> void {
        std::visit([&value](const auto& item) { value = item; }, event);
    }

    auto from_json(const nlohmann::json& value, StreamEvent& event) -> void {
        const auto type = require_type(value);

        if (type == "assistant_delta") {
            event = value.get<AssistantTextDelta>();
            return;
        }

        if (type == "assistant_complete") {
            event = value.get<AssistantTurnComplete>();
            return;
        }

        if (type == "tool_start") {
            event = value.get<ToolExecutionStared>();
            return;
        }

        if (type == "tool_complete") {
            event = value.get<ToolExecutionComplete>();
            return;
        }

        throw std::invalid_argument{"unknown stream event type: " + type};
    }

}  // namespace codeharness::engine

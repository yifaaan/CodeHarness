#include "codeharness/engine/stream_event.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <string>
#include <utility>

#include "codeharness/engine/message_json.h"

namespace codeharness::engine {
namespace {

    [[nodiscard]] auto require_type(const nlohmann::json& value) -> absl::StatusOr<std::string> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("stream event must be a JSON object");
        }
        if (!value.contains("type") || !value.at("type").is_string()) {
            return absl::InvalidArgumentError("stream event is missing string field: type");
        }

        return value.at("type").get<std::string>();
    }

    // Payload-only helpers: caller has already matched "type" via require_type + string compare.
    [[nodiscard]] auto assistant_text_delta_body(const nlohmann::json& value) -> AssistantTextDelta {
        return AssistantTextDelta{
            .text = value.value("text", ""),
        };
    }

    [[nodiscard]] auto assistant_turn_complete_body(const nlohmann::json& value)
        -> absl::StatusOr<AssistantTurnComplete> {
        if (!value.contains("message")) {
            return absl::InvalidArgumentError("assistant_complete is missing message");
        }

        auto message = conversation_message_from_json(value.at("message"));
        if (!message.ok()) {
            return message.status();
        }

        return AssistantTurnComplete{
            .message = std::move(*message),
            .usage = value.value("usage", nlohmann::json::object()).get<UsageSnapshot>(),
        };
    }

    [[nodiscard]] auto tool_execution_started_body(const nlohmann::json& value) -> ToolExecutionStared {
        return ToolExecutionStared{
            .tool_name = value.value("tool_name", ""),
            .tool_input = value.value("tool_input", nlohmann::json::object()),
        };
    }

    [[nodiscard]] auto tool_execution_complete_body(const nlohmann::json& value)
        -> ToolExecutionComplete {
        return ToolExecutionComplete{
            .tool_name = value.value("tool_name", ""),
            .output = value.value("output", ""),
            .is_error = value.value("is_error", false),
        };
    }

    [[nodiscard]] auto assistant_text_delta_from_json(const nlohmann::json& value)
        -> absl::StatusOr<AssistantTextDelta> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "assistant_delta") {
            return absl::InvalidArgumentError("stream event is not assistant_delta");
        }
        return assistant_text_delta_body(value);
    }

    [[nodiscard]] auto assistant_turn_complete_from_json(const nlohmann::json& value)
        -> absl::StatusOr<AssistantTurnComplete> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "assistant_complete") {
            return absl::InvalidArgumentError("stream event is not assistant_complete");
        }
        return assistant_turn_complete_body(value);
    }

    [[nodiscard]] auto tool_execution_started_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ToolExecutionStared> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "tool_start") {
            return absl::InvalidArgumentError("stream event is not tool_start");
        }
        return tool_execution_started_body(value);
    }

    [[nodiscard]] auto tool_execution_complete_from_json(const nlohmann::json& value)
        -> absl::StatusOr<ToolExecutionComplete> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }
        if (*type != "tool_complete") {
            return absl::InvalidArgumentError("stream event is not tool_complete");
        }
        return tool_execution_complete_body(value);
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
        const auto parsed = assistant_text_delta_from_json(value);
        delta = parsed.ok() ? std::move(*parsed) : AssistantTextDelta{};
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
        const auto parsed = assistant_turn_complete_from_json(value);
        complete = parsed.ok() ? std::move(*parsed) : AssistantTurnComplete{};
    }

    auto to_json(nlohmann::json& value, const ToolExecutionStared& started) -> void {
        value = {
            {"type", "tool_start"},
            {"tool_name", started.tool_name},
            {"tool_input", started.tool_input},
        };
    }

    auto from_json(const nlohmann::json& value, ToolExecutionStared& started) -> void {
        const auto parsed = tool_execution_started_from_json(value);
        started = parsed.ok() ? std::move(*parsed) : ToolExecutionStared{};
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
        const auto parsed = tool_execution_complete_from_json(value);
        complete = parsed.ok() ? std::move(*parsed) : ToolExecutionComplete{};
    }

    auto to_json(nlohmann::json& value, const StreamEvent& event) -> void {
        std::visit([&value](const auto& item) { value = item; }, event);
    }

    auto from_json(const nlohmann::json& value, StreamEvent& event) -> void {
        const auto parsed = stream_event_from_json(value);
        event = parsed.ok() ? std::move(*parsed) : StreamEvent{AssistantTextDelta{}};
    }

    auto stream_event_from_json(const nlohmann::json& value) -> absl::StatusOr<StreamEvent> {
        const auto type = require_type(value);
        if (!type.ok()) {
            return type.status();
        }

        if (*type == "assistant_delta") {
            return StreamEvent{assistant_text_delta_body(value)};
        }
        if (*type == "assistant_complete") {
            const auto parsed = assistant_turn_complete_body(value);
            if (!parsed.ok()) {
                return parsed.status();
            }
            return StreamEvent{std::move(*parsed)};
        }
        if (*type == "tool_start") {
            return StreamEvent{tool_execution_started_body(value)};
        }
        if (*type == "tool_complete") {
            return StreamEvent{tool_execution_complete_body(value)};
        }

        return absl::InvalidArgumentError("unknown stream event type: " + *type);
    }

}  // namespace codeharness::engine

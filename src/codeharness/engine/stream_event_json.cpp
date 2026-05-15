#include "codeharness/engine/stream_event.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <string>
#include <utility>

#include "codeharness/engine/message_json.h"

namespace codeharness::engine {

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
        auto parsed = stream_event_from_json(value);
        const auto* item = parsed.ok() ? std::get_if<AssistantTextDelta>(&*parsed) : nullptr;
        delta = item != nullptr ? *item : AssistantTextDelta{};
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
        auto parsed = stream_event_from_json(value);
        const auto* item = parsed.ok() ? std::get_if<AssistantTurnComplete>(&*parsed) : nullptr;
        complete = item != nullptr ? *item : AssistantTurnComplete{};
    }

    auto to_json(nlohmann::json& value, const ToolExecutionStared& started) -> void {
        value = {
            {"type", "tool_start"},
            {"tool_name", started.tool_name},
            {"tool_input", started.tool_input},
        };
    }

    auto from_json(const nlohmann::json& value, ToolExecutionStared& started) -> void {
        auto parsed = stream_event_from_json(value);
        const auto* item = parsed.ok() ? std::get_if<ToolExecutionStared>(&*parsed) : nullptr;
        started = item != nullptr ? *item : ToolExecutionStared{};
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
        auto parsed = stream_event_from_json(value);
        const auto* item = parsed.ok() ? std::get_if<ToolExecutionComplete>(&*parsed) : nullptr;
        complete = item != nullptr ? *item : ToolExecutionComplete{};
    }

    auto to_json(nlohmann::json& value, const StreamEvent& event) -> void {
        std::visit([&value](const auto& item) { value = item; }, event);
    }

    auto from_json(const nlohmann::json& value, StreamEvent& event) -> void {
        const auto parsed = stream_event_from_json(value);
        event = parsed.ok() ? std::move(*parsed) : StreamEvent{AssistantTextDelta{}};
    }

    auto stream_event_from_json(const nlohmann::json& value) -> absl::StatusOr<StreamEvent> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("stream event must be a JSON object");
        }
        if (!value.contains("type") || !value.at("type").is_string()) {
            return absl::InvalidArgumentError("stream event is missing string field: type");
        }

        const auto type = value.at("type").get<std::string>();

        if (type == "assistant_delta") {
            return StreamEvent{
                AssistantTextDelta{
                    .text = value.value("text", ""),
                },
            };
        }
        if (type == "assistant_complete") {
            if (!value.contains("message")) {
                return absl::InvalidArgumentError("assistant_complete is missing message");
            }

            auto message = conversation_message_from_json(value.at("message"));
            if (!message.ok()) {
                return message.status();
            }

            return StreamEvent{
                AssistantTurnComplete{
                    .message = std::move(*message),
                    .usage = value.value("usage", nlohmann::json::object()).get<UsageSnapshot>(),
                },
            };
        }
        if (type == "tool_start") {
            return StreamEvent{
                ToolExecutionStared{
                    .tool_name = value.value("tool_name", ""),
                    .tool_input = value.value("tool_input", nlohmann::json::object()),
                },
            };
        }
        if (type == "tool_complete") {
            return StreamEvent{
                ToolExecutionComplete{
                    .tool_name = value.value("tool_name", ""),
                    .output = value.value("output", ""),
                    .is_error = value.value("is_error", false),
                },
            };
        }

        return absl::InvalidArgumentError("unknown stream event type: " + type);
    }

}  // namespace codeharness::engine

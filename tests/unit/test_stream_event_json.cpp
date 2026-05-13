#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "codeharness/engine/stream_event.h"
#include "codeharness/ui/stream_json_renderer.h"

using namespace codeharness;

TEST_CASE("stream event json round trips through nlohmann conversions") {
    const auto original = engine::StreamEvent{
        engine::AssistantTurnComplete{
            .message = engine::ConversationMessage{
                .role = engine::MessageRole::assistant,
                .content = {
                    engine::TextBlock{.text = "done"},
                },
            },
            .usage =
                engine::UsageSnapshot{
                    .input_tokens = 12,
                    .output_tokens = 7,
                },
        },
    };

    const auto serialized = nlohmann::json(original);
    CHECK(serialized.at("type").get<std::string>() == "assistant_complete");
    CHECK(serialized.at("usage").at("input_tokens").get<int>() == 12);
    CHECK(serialized.at("text").get<std::string>() == "done");

    const auto parsed = engine::stream_event_from_json(serialized);
    REQUIRE(parsed.ok());
    const auto* complete = std::get_if<engine::AssistantTurnComplete>(&*parsed);
    REQUIRE(complete != nullptr);
    CHECK(complete->message.text() == "done");
    CHECK(complete->usage.input_tokens == 12);
    CHECK(complete->usage.output_tokens == 7);
}

TEST_CASE("stream json renderer uses nlohmann serialization interfaces") {
    const auto event = engine::StreamEvent{
        engine::ToolExecutionComplete{
            .tool_name = "read_file",
            .output = "hello",
            .is_error = false,
        },
    };

    const auto rendered = ui::to_stream_json(event);
    CHECK(rendered == nlohmann::json(event));
}

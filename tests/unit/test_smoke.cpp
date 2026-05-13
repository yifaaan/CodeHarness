#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <absl/strings/str_cat.h>
#include <doctest/doctest.h>
#include <fmt/core.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <variant>
#include <vector>

#include "codeharness/api/mock_client.h"
#include "codeharness/engine/message.h"

using namespace codeharness;

TEST_CASE("mock client streams text and records requests") {
    auto client = api::MockClient{
        std::deque<api::MockClient::Response>{
            api::MockClient::Response{
                .message =
                    engine::ConversationMessage{
                        .role = engine::MessageRole::assistant,
                        .content = {engine::TextBlock{.text = "hello from mock"}},
                    },
                .usage =
                    engine::UsageSnapshot{
                        .input_tokens = 2,
                        .output_tokens = 3,
                    },
                .stop_reason = "end_turn",
            },
        },
    };

    std::vector<api::ApiStreamEvent> events;
    const auto status = client.stream_message(
        api::MessageRequest{
            .model = "mock-model",
            .messages = {},
            .system_prompt = "system",
            .max_tokens = 128,
            .tools = nlohmann::json::array(),
        },
        [&](const api::ApiStreamEvent& event) { events.push_back(event); });
    REQUIRE(status.ok());

    REQUIRE(events.size() == 2);

    const auto* delta = std::get_if<engine::AssistantTextDelta>(&events[0]);
    REQUIRE(delta != nullptr);
    CHECK(delta->text == "hello from mock");

    const auto* complete = std::get_if<api::MessageComplete>(&events[1]);
    REQUIRE(complete != nullptr);
    CHECK(complete->message.text() == "hello from mock");
    CHECK(complete->usage.input_tokens == 2);
    CHECK(complete->usage.output_tokens == 3);
    CHECK(complete->stop_reason == "end_turn");

    const auto requests = client.requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].model == "mock-model");
    CHECK(requests[0].system_prompt == "system");
}

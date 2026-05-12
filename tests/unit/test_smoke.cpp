#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <absl/strings/str_cat.h>
#include <doctest/doctest.h>
#include <fmt/core.h>

#include <deque>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

#include "codeharness/api/mock_client.h"
#include "codeharness/engine/message.h"

TEST_CASE("json dependency is available") {
    const nlohmann::json value{{"name", "CodeHarness"}, {"cpp", 23}};

    CHECK(value.at("name").get<std::string>() == "CodeHarness");
    CHECK(value.at("cpp").get<int>() == 23);
}

TEST_CASE("fmt dependency is available") {
    CHECK(fmt::format("{}{}", "agent-", "harness") == "agent-harness");
}

TEST_CASE("abseil dependency is available") {
    CHECK(absl::StrCat("code", "-", "harness") == "code-harness");
}

TEST_CASE("mock client streams text and records requests") {
    auto client = codeharness::api::MockClient{
        std::deque<codeharness::api::MockClient::Response>{
            codeharness::api::MockClient::Response{
                .message =
                    codeharness::engine::ConversationMessage{
                        .role = codeharness::engine::MessageRole::assistent,
                        .content = {codeharness::engine::TextBlock{.text = "hello from mock"}},
                    },
                .usage =
                    codeharness::engine::UsageSnapshot{
                        .input_tokens = 2,
                        .output_tokens = 3,
                    },
                .stop_reason = "end_turn",
            },
        },
    };

    std::vector<codeharness::api::ApiStreamEvent> events;
    client.stream_message(
        codeharness::api::MessageRequest{
            .model = "mock-model",
            .messages = {},
            .system_prompt = "system",
            .max_tokens = 128,
            .tools = nlohmann::json::array(),
        },
        [&](const codeharness::api::ApiStreamEvent& event) { events.push_back(event); });

    REQUIRE(events.size() == 2);

    const auto* delta = std::get_if<codeharness::engine::AssistantTextDelta>(&events[0]);
    REQUIRE(delta != nullptr);
    CHECK(delta->text == "hello from mock");

    const auto* complete = std::get_if<codeharness::api::MessageComplete>(&events[1]);
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

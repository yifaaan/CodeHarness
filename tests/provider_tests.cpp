#include "test_support.h"

TEST_CASE("echo provider returns latest user text")
{
    codeharness::EchoProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::System, "system rules"));
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    auto result = provider.generate(std::span<const codeharness::Message>(messages));

    REQUIRE(result.has_value());
    CHECK(result->role == codeharness::Role::Assistant);
    CHECK(codeharness::collect_text(*result) == "hello");
}

TEST_CASE("echo provider streams text delta")
{
    codeharness::EchoProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    std::string streamed_text;
    bool finished = false;

    auto result = provider.stream(messages, [&](const codeharness::ProviderEvent& event) {
        if (auto delta = std::get_if<codeharness::AssistantTextDelta>(&event))
        {
            streamed_text += delta->text;
        }

        if (std::holds_alternative<codeharness::MessageFinished>(event))
        {
            finished = true;
        }
    });

    REQUIRE(result.has_value());
    CHECK(streamed_text == "hello");
    CHECK(finished);
}

namespace
{

class ToolDeltaBeforeStartProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = "{}",
            });
        sink(codeharness::MessageFinished{});

        return {};
    }
};

} // namespace

TEST_CASE("provider generate rejects tool input before tool start")
{
    ToolDeltaBeforeStartProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    auto result = provider.generate(std::span<const codeharness::Message>(messages));

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Provider);
}

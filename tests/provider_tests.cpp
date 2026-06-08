#include "codeharness/network/http_client.h"
#include "codeharness/network/sse_parser.h"
#include "codeharness/provider/anthropic_serialize.h"
#include "codeharness/provider/anthropic_stream_parser.h"
#include "codeharness/provider/openai_serialize.h"
#include "codeharness/provider/openai_stream_parser.h"
#include "codeharness/provider/provider_http.h"
#include "codeharness/runtime/runtime.h"

#include "test_support.h"

#include <algorithm>

namespace
{

auto make_runtime_bundle(TempDir& temp, codeharness::ProviderConfig provider_config) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "repo";
    const auto memory_root = temp.path / "memory-root";
    std::filesystem::create_directories(repo);

    return codeharness::runtime::create_runtime_bundle(
        codeharness::runtime::RuntimeBundleOptions{
            .cwd = repo,
            .memory_root = memory_root,
            .load_default_user_plugins = false,
            .provider_config = std::move(provider_config),
        });
}

auto has_event_text(const std::vector<codeharness::ProviderEvent>& events, std::string_view text) -> bool
{
    return std::ranges::any_of(events, [&](const auto& event) {
        const auto* delta = std::get_if<codeharness::AssistantTextDelta>(&event);
        return delta != nullptr && delta->text == text;
    });
}

auto has_message_finished(const std::vector<codeharness::ProviderEvent>& events) -> bool
{
    return std::ranges::any_of(events, [](const auto& event) { return std::holds_alternative<codeharness::MessageFinished>(event); });
}

auto last_usage(const std::vector<codeharness::ProviderEvent>& events) -> codeharness::ProviderUsage
{
    codeharness::ProviderUsage usage;
    for (const auto& event : events)
    {
        if (const auto* updated = std::get_if<codeharness::ProviderUsage>(&event))
        {
            usage = *updated;
        }
    }
    return usage;
}

} // namespace

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

TEST_CASE("echo provider does not report usage")
{
    codeharness::EchoProvider provider;
    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    bool saw_usage = false;
    auto result = provider.stream(messages, [&](const codeharness::ProviderEvent& event) {
        saw_usage = saw_usage || std::holds_alternative<codeharness::ProviderUsage>(event);
    });

    REQUIRE(result.has_value());
    CHECK_FALSE(saw_usage);
}

namespace
{

class ToolDeltaBeforeStartProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink) -> codeharness::Result<void> override
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

TEST_CASE("provider http client validates URLs before opening a socket")
{
    codeharness::network::HttpClient client;

    auto unsupported = client.get("ftp://example.com/v1", {});
    REQUIRE(!unsupported.has_value());
    CHECK(unsupported.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto hostless = client.get("https:///v1/responses", {});
    REQUIRE(!hostless.has_value());
    CHECK(hostless.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("provider HTTP helpers join endpoints and preserve API error messages")
{
    CHECK(codeharness::provider_endpoint_url("", "responses", "https://api.openai.com/v1/responses") == "https://api.openai.com/v1/responses");
    CHECK(codeharness::provider_endpoint_url("https://api.example.test/v1", "responses", "") == "https://api.example.test/v1/responses");
    CHECK(codeharness::provider_endpoint_url("https://api.example.test/v1/messages", "messages", "") == "https://api.example.test/v1/messages");

    // When base_url has no path and default_url has a prefix like "/v1",
    // the prefix should be auto-inserted so bare host URLs still work.
    CHECK(codeharness::provider_endpoint_url("https://custom.host", "responses", "https://api.openai.com/v1/responses") == "https://custom.host/v1/responses");
    CHECK(codeharness::provider_endpoint_url("https://custom.host", "messages", "https://api.anthropic.com/v1/messages") == "https://custom.host/v1/messages");

    codeharness::network::HttpResponse response;
    response.status_code = 401;
    response.body = R"({"error":{"message":"bad key"}})";

    CHECK(codeharness::provider_http_error_message("OpenAI", response) == "OpenAI API returned status 401: bad key");
}

TEST_CASE("provider HTTP retry helper distinguishes retryable and non-retryable status codes")
{
    CHECK_FALSE(codeharness::should_retry_http_status(200));
    CHECK_FALSE(codeharness::should_retry_http_status(400));
    CHECK_FALSE(codeharness::should_retry_http_status(401));
    CHECK_FALSE(codeharness::should_retry_http_status(403));
    CHECK_FALSE(codeharness::should_retry_http_status(404));
    CHECK_FALSE(codeharness::should_retry_http_status(422));

    CHECK(codeharness::should_retry_http_status(429));
    CHECK(codeharness::should_retry_http_status(500));
    CHECK(codeharness::should_retry_http_status(502));
    CHECK(codeharness::should_retry_http_status(503));
    CHECK(codeharness::should_retry_http_status(504));

    // 1xx, 2xx, 3xx are never retryable as errors
    CHECK_FALSE(codeharness::should_retry_http_status(100));
    CHECK_FALSE(codeharness::should_retry_http_status(301));
    CHECK_FALSE(codeharness::should_retry_http_status(307));
}

TEST_CASE("provider HTTP retry delay increases exponentially with jitter")
{
    using namespace std::chrono_literals;

    auto d1 = codeharness::provider_retry_delay(0, 1s, 30s);
    CHECK(d1 >= 750ms);
    CHECK(d1 <= 1250ms);

    auto d2 = codeharness::provider_retry_delay(1, 1s, 30s);
    CHECK(d2 >= 1500ms);
    CHECK(d2 <= 2500ms);

    auto d3 = codeharness::provider_retry_delay(2, 1s, 30s);
    CHECK(d3 >= 3000ms);
    CHECK(d3 <= 5000ms);
}

TEST_CASE("provider HTTP retry delay is capped at max_delay")
{
    using namespace std::chrono_literals;

    // attempt 5 with base 1s → 32s before cap → capped at 10s
    auto d = codeharness::provider_retry_delay(5, 1s, 10s);
    CHECK(d <= 10s);
    CHECK(d >= 7500ms);
}

TEST_CASE("provider openai responses serialization maps messages and tools")
{
    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::System, "You are concise."));
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "Read the file."));

    codeharness::Message assistant;
    assistant.role = codeharness::Role::Assistant;
    assistant.content.push_back(codeharness::TextBlock{"Using a tool."});
    assistant.content.push_back(
        codeharness::ToolUseBlock{
            .id = "call_1",
            .name = "read_file",
            .input_json = R"({"path":"main.cpp"})",
        });
    messages.push_back(std::move(assistant));
    messages.push_back(codeharness::make_tool_result_message({codeharness::ToolResultBlock{.tool_use_id = "call_1", .content = "file text", .is_error = false}}));

    auto input = codeharness::serialize_openai_input(messages);
    REQUIRE(input.is_array());
    CHECK(input.at(0).at("role") == "system");
    CHECK(input.at(0).at("content") == "You are concise.");
    CHECK(input.at(1).at("role") == "user");
    CHECK(input.at(2).at("role") == "assistant");
    CHECK(input.at(3).at("type") == "function_call");
    CHECK(input.at(3).at("call_id") == "call_1");
    CHECK(input.at(3).at("name") == "read_file");
    CHECK(nlohmann::json::parse(input.at(3).at("arguments").get<std::string>()).at("path") == "main.cpp");
    CHECK(input.at(4).at("type") == "function_call_output");
    CHECK(input.at(4).at("output") == "file text");

    const auto tools = codeharness::serialize_openai_tools({{"read_file", "Read a file"}});
    REQUIRE(tools.size() == 1);
    CHECK(tools.at(0).at("type") == "function");
    CHECK(tools.at(0).at("name") == "read_file");
    CHECK(tools.at(0).at("parameters").at("additionalProperties") == true);
    CHECK(tools.at(0).at("strict") == false);
}

TEST_CASE("provider openai responses stream parser handles text tool calls completion and errors")
{
    codeharness::OpenAIStreamParser parser;

    auto text = parser.feed(
        R"(data: {"type":"response.output_text.delta","delta":"Hello"})"
        "\n\n");
    CHECK(text.error.empty());
    REQUIRE(text.events.size() == 1);
    CHECK(has_event_text(text.events, "Hello"));

    auto tool = parser.feed(
        R"(data: {"type":"response.output_item.added","item":{"id":"fc_1","type":"function_call","call_id":"call_1","name":"read_file"}})"
        "\n\n"
        R"(data: {"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"{\"path\""})"
        "\n\n"
        R"(data: {"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":":\"main.cpp\"}"})"
        "\n\n"
        R"(data: {"type":"response.function_call_arguments.done","item_id":"fc_1"})"
        "\n\n"
        R"(data: {"type":"response.completed"})"
        "\n\n");

    CHECK(tool.error.empty());
    CHECK(tool.done);
    bool started = false;
    bool finished = false;
    std::string arguments;
    for (const auto& event : tool.events)
    {
        if (const auto* start = std::get_if<codeharness::ToolUseStarted>(&event))
        {
            started = true;
            CHECK(start->id == "call_1");
            CHECK(start->name == "read_file");
        }
        if (const auto* delta = std::get_if<codeharness::ToolUseInputDelta>(&event))
        {
            arguments += delta->input_json_delta;
        }
        if (const auto* finish = std::get_if<codeharness::ToolUseFinished>(&event))
        {
            finished = true;
            CHECK(finish->id == "call_1");
        }
    }
    CHECK(started);
    CHECK(finished);
    CHECK(arguments == R"({"path":"main.cpp"})");
    CHECK(has_message_finished(tool.events));

    codeharness::OpenAIStreamParser error_parser;
    auto error = error_parser.feed(
        R"(data: {"type":"response.failed","response":{"error":{"message":"Invalid API key"}}})"
        "\n\n");
    CHECK(error.done);
    CHECK(error.error.find("Invalid API key") != std::string::npos);
}

TEST_CASE("provider openai stream parser emits usage from completed event")
{
    codeharness::OpenAIStreamParser parser;

    auto parsed = parser.feed(
        R"(data: {"type":"response.completed","response":{"usage":{"input_tokens":11,"output_tokens":7,"total_tokens":18}}})"
        "\n\n");

    CHECK(parsed.error.empty());
    CHECK(parsed.done);
    const auto usage = last_usage(parsed.events);
    CHECK(usage.input_tokens == 11);
    CHECK(usage.output_tokens == 7);
    CHECK(usage.total_tokens == 18);
    CHECK(has_message_finished(parsed.events));
}

TEST_CASE("provider openai stream parser recovers failed event after malformed data line")
{
    codeharness::OpenAIStreamParser parser;

    auto parsed = parser.feed(
        "data: {\"type\":\"response.output_ite\n"
        "event: response.failed\n"
        R"(data: {"type":"response.failed","response":{"error":{"message":"Upstream request failed"}}})"
        "\n\n");

    CHECK(parsed.done);
    CHECK(parsed.error == "Upstream request failed");
}

TEST_CASE("provider anthropic serialization maps messages tools and system prompt")
{
    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::System, "System rules."));
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "Hello"));

    codeharness::Message assistant;
    assistant.role = codeharness::Role::Assistant;
    assistant.content.push_back(codeharness::TextBlock{"I will inspect it."});
    assistant.content.push_back(
        codeharness::ToolUseBlock{
            .id = "toolu_1",
            .name = "read_file",
            .input_json = R"({"path":"main.cpp"})",
        });
    messages.push_back(std::move(assistant));
    messages.push_back(codeharness::make_tool_result_message({codeharness::ToolResultBlock{.tool_use_id = "toolu_1", .content = "file text", .is_error = true}}));

    CHECK(codeharness::serialize_anthropic_system(messages) == "System rules.");

    auto serialized = codeharness::serialize_anthropic_messages(messages);
    REQUIRE(serialized.is_array());
    CHECK(serialized.at(0).at("role") == "user");
    CHECK(serialized.at(0).at("content").at(0).at("text") == "Hello");
    CHECK(serialized.at(1).at("role") == "assistant");
    CHECK(serialized.at(1).at("content").at(1).at("type") == "tool_use");
    CHECK(serialized.at(1).at("content").at(1).at("input").at("path") == "main.cpp");
    CHECK(serialized.at(2).at("role") == "user");
    CHECK(serialized.at(2).at("content").at(0).at("type") == "tool_result");
    CHECK(serialized.at(2).at("content").at(0).at("is_error") == true);

    auto tools = codeharness::serialize_anthropic_tools({{"read_file", "Read a file"}});
    REQUIRE(tools.size() == 1);
    CHECK(tools.at(0).at("name") == "read_file");
    CHECK(tools.at(0).at("input_schema").at("additionalProperties") == true);
}

TEST_CASE("provider anthropic stream parser handles text tool use completion and errors")
{
    codeharness::AnthropicStreamParser parser;

    auto parsed = parser.feed(
        R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})"
        "\n\n"
        R"(data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_1","name":"read_file"}})"
        "\n\n"
        R"(data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"path\":\"main.cpp\"}"}})"
        "\n\n"
        R"(data: {"type":"content_block_stop","index":1})"
        "\n\n"
        R"(data: {"type":"message_stop"})"
        "\n\n");

    CHECK(parsed.error.empty());
    CHECK(parsed.done);
    CHECK(has_event_text(parsed.events, "Hello"));
    CHECK(has_message_finished(parsed.events));

    bool started = false;
    bool finished = false;
    std::string arguments;
    for (const auto& event : parsed.events)
    {
        if (const auto* start = std::get_if<codeharness::ToolUseStarted>(&event))
        {
            started = true;
            CHECK(start->id == "toolu_1");
            CHECK(start->name == "read_file");
        }
        if (const auto* delta = std::get_if<codeharness::ToolUseInputDelta>(&event))
        {
            arguments += delta->input_json_delta;
        }
        if (const auto* finish = std::get_if<codeharness::ToolUseFinished>(&event))
        {
            finished = true;
            CHECK(finish->id == "toolu_1");
        }
    }
    CHECK(started);
    CHECK(finished);
    CHECK(arguments == R"({"path":"main.cpp"})");

    codeharness::AnthropicStreamParser error_parser;
    auto error = error_parser.feed(
        R"(data: {"type":"error","error":{"message":"bad request"}})"
        "\n\n");
    CHECK(error.done);
    CHECK(error.error.find("bad request") != std::string::npos);

    codeharness::AnthropicStreamParser unsupported_parser;
    auto unsupported = unsupported_parser.feed(
        R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"hidden"}})"
        "\n\n");
    CHECK(unsupported.done);
    CHECK(unsupported.error.find("unsupported Anthropic stream event") != std::string::npos);
}

TEST_CASE("provider anthropic stream parser accumulates usage from start and delta events")
{
    codeharness::AnthropicStreamParser parser;

    auto parsed = parser.feed(
        R"(data: {"type":"message_start","message":{"usage":{"input_tokens":23,"output_tokens":1}}})"
        "\n\n"
        R"(data: {"type":"message_delta","usage":{"output_tokens":9}})"
        "\n\n"
        R"(data: {"type":"message_stop"})"
        "\n\n");

    CHECK(parsed.error.empty());
    CHECK(parsed.done);
    const auto usage = last_usage(parsed.events);
    CHECK(usage.input_tokens == 23);
    CHECK(usage.output_tokens == 9);
    CHECK(usage.total_tokens == 32);
}

TEST_CASE("provider sse parser handles order partial chunks comments and multi-line data")
{
    codeharness::network::SseParser parser;

    auto first = parser.feed(
        ": comment\n"
        "event: first\n"
        "data: one\n"
        "data: two\n\n"
        "event: second\n"
        "da");
    REQUIRE(first.size() == 1);
    CHECK(first.at(0).event == "first");
    CHECK(first.at(0).data == "one\ntwo");

    auto second = parser.feed("ta: done\n\n");
    REQUIRE(second.size() == 1);
    CHECK(second.at(0).event == "second");
    CHECK(second.at(0).data == "done");
}

TEST_CASE("runtime provider selection validates configured providers")
{
    TempDir temp{"codeharness-provider-selection-test"};

    auto echo = make_runtime_bundle(temp, codeharness::ProviderConfig{.type = "echo"});
    REQUIRE(echo.has_value());

    auto missing_openai_key = make_runtime_bundle(temp, codeharness::ProviderConfig{.type = "openai"});
    REQUIRE(!missing_openai_key.has_value());
    CHECK(missing_openai_key.error().kind == codeharness::ErrorKind::Config);

    auto missing_anthropic_key = make_runtime_bundle(temp, codeharness::ProviderConfig{.type = "anthropic"});
    REQUIRE(!missing_anthropic_key.has_value());
    CHECK(missing_anthropic_key.error().kind == codeharness::ErrorKind::Config);

    auto unknown = make_runtime_bundle(temp, codeharness::ProviderConfig{.type = "made-up"});
    REQUIRE(!unknown.has_value());
    CHECK(unknown.error().kind == codeharness::ErrorKind::InvalidArgument);
}

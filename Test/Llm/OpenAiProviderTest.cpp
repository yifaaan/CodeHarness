#include "Llm/OpenAiProvider.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "Llm/HttpClient.h"
#include "Llm/Types.h"

namespace llm = codeharness::llm;

namespace
{

	class MockHttpClient : public llm::HttpClient
	{
	public:
		std::string capturedBody;
		std::vector<std::pair<std::string, std::string>> capturedHeaders;
		int responseStatus = 200;
		std::vector<std::string> responseChunks;

		absl::StatusOr<llm::HttpResponse> Request(const llm::HttpRequest& req) override
		{
			capturedBody = req.body;
			capturedHeaders = req.headers;
			return llm::HttpResponse{responseStatus, {}, ""};
		}

		absl::StatusOr<llm::HttpResponse> StreamRequest(const llm::HttpRequest& req, const llm::StreamChunkCallback& onChunk, std::stop_token = {}) override
		{
			capturedBody = req.body;
			capturedHeaders = req.headers;
			for (const auto& chunk : responseChunks)
			{
				if (!onChunk(chunk))
					break;
			}
			return llm::HttpResponse{responseStatus, {}, ""};
		}
	};

	struct CallbackState
	{
		std::string text;
		struct ToolCall
		{
			int index;
			std::string id;
			std::string name;
			std::string args;
		};
		std::vector<ToolCall> toolCalls;
		std::optional<llm::FinishReason> finish;
		std::optional<llm::TokenUsage> usage;

		llm::StreamCallbacks ToCallbacks()
		{
			return {
				.onText = [this](std::string_view t) { text += t; },
				.onThink = {},
				.onToolCallStart =
					[this](int idx, std::string_view id, std::string_view name) {
						toolCalls.push_back({idx, std::string(id), std::string(name), ""});
					},
				.onToolCallDelta =
					[this](int idx, std::string_view args) {
						for (auto& tc : toolCalls)
						{
							if (tc.index == idx)
							{
								tc.args += args;
								return;
							}
						}
					},
				.onFinish =
					[this](llm::FinishReason f, const llm::TokenUsage& u) {
						finish = f;
						usage = u;
					},
			};
		}
	};

	llm::OpenAiConfig MakeConfig()
	{
		return {.apiKey = "test-key", .host = "api.openai.com", .model = "gpt-4o"};
	}

	std::string SSE(const std::string& Data)
	{
		return "data: " + Data + "\n\n";
	}

} // namespace

TEST_CASE("OpenAiProvider: basic text streaming")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{"role":"assistant"}}]})"),
		SSE(R"({"choices":[{"delta":{"content":"Hello"}}]})"),
		SSE(R"({"choices":[{"delta":{"content":" world"}}]})"),
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;
	auto callbacks = state.ToCallbacks();

	auto status = provider.Generate("You are helpful.", {}, {}, callbacks);
	CHECK(status.ok());
	CHECK(state.text == "Hello world");
	REQUIRE(state.finish.has_value());
	CHECK(*state.finish == llm::FinishReason::Completed);
}

TEST_CASE("OpenAiProvider: tool call streaming")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read_file","arguments":""}}]}}]})"),
		SSE(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"path"}}]}}]})"),
		SSE(R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\":\"a.txt\"}"}}]}}]})"),
		SSE(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;
	auto callbacks = state.ToCallbacks();

	auto status = provider.Generate("", {}, {}, callbacks);
	CHECK(status.ok());
	REQUIRE(state.toolCalls.size() == 1);
	CHECK(state.toolCalls[0].index == 0);
	CHECK(state.toolCalls[0].id == "call_1");
	CHECK(state.toolCalls[0].name == "read_file");
	CHECK(state.toolCalls[0].args == "{\"path\":\"a.txt\"}");
	REQUIRE(state.finish.has_value());
	CHECK(*state.finish == llm::FinishReason::ToolCalls);
}

TEST_CASE("OpenAiProvider: usage tracking")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{"content":"hi"}}]})"),
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":50,"completion_tokens":5,"total_tokens":55}})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;
	auto callbacks = state.ToCallbacks();

	auto status = provider.Generate("", {}, {}, callbacks);
	CHECK(status.ok());
	REQUIRE(state.usage.has_value());
	CHECK(state.usage->output == 5);
	CHECK(state.usage->inputOther == 50);
}

TEST_CASE("OpenAiProvider: request body includes model and stream")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	CHECK(provider.Generate("system prompt", {}, {}, state.ToCallbacks()).ok());

	CHECK(mock.capturedBody.find("\"model\":\"gpt-4o\"") != std::string::npos);
	CHECK(mock.capturedBody.find("\"stream\":true") != std::string::npos);
	CHECK(mock.capturedBody.find("include_usage") != std::string::npos);
}

TEST_CASE("OpenAiProvider: auth header")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	CHECK(provider.Generate("", {}, {}, state.ToCallbacks()).ok());

	bool foundAuth = false;
	for (const auto& [key, value] : mock.capturedHeaders)
	{
		if (key == "Authorization" && value == "Bearer test-key")
		{
			foundAuth = true;
			break;
		}
	}
	CHECK(foundAuth);
}

TEST_CASE("OpenAiProvider: no API key returns error")
{
	MockHttpClient mock;
	llm::OpenAiConfig config{.apiKey = "", .host = "api.openai.com", .model = "gpt-4o"};

	llm::OpenAiProvider provider(config, &mock);
	CallbackState state;

	auto status = provider.Generate("", {}, {}, state.ToCallbacks());
	CHECK_FALSE(status.ok());
	CHECK(status.code() == absl::StatusCode::kFailedPrecondition);
}

TEST_CASE("OpenAiProvider: HTTP 401 returns unauthenticated error")
{
	MockHttpClient mock;
	mock.responseStatus = 401;
	mock.responseChunks = {R"({"error":{"message":"invalid api key"}})"};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	auto status = provider.Generate("", {}, {}, state.ToCallbacks());
	CHECK_FALSE(status.ok());
	CHECK(status.code() == absl::StatusCode::kUnauthenticated);
}

TEST_CASE("OpenAiProvider: HTTP 429 returns resource exhausted")
{
	MockHttpClient mock;
	mock.responseStatus = 429;
	mock.responseChunks = {R"({"error":{"message":"rate limited"}})"};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	auto status = provider.Generate("", {}, {}, state.ToCallbacks());
	CHECK_FALSE(status.ok());
	CHECK(status.code() == absl::StatusCode::kResourceExhausted);
}

TEST_CASE("OpenAiProvider: missing finish_reason returns error")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{"content":"partial"}}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	auto status = provider.Generate("", {}, {}, state.ToCallbacks());
	CHECK_FALSE(status.ok());
	CHECK(status.code() == absl::StatusCode::kInternal);
}

TEST_CASE("OpenAiProvider: empty stream returns error")
{
	MockHttpClient mock;
	mock.responseChunks = {};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	auto status = provider.Generate("", {}, {}, state.ToCallbacks());
	CHECK_FALSE(status.ok());
}

TEST_CASE("OpenAiProvider: Name and ModelName")
{
	MockHttpClient mock;
	llm::OpenAiProvider provider(MakeConfig(), &mock);

	CHECK(provider.Name() == "openai");
	CHECK(provider.ModelName() == "gpt-4o");
}

TEST_CASE("OpenAiProvider: tools included in request body when provided")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	llm::Tool tool;
	tool.name = "read_file";
	tool.description = "Reads a file";
	tool.inputSchema = nlohmann::json::parse(R"({"type":"object","properties":{"path":{"type":"string"}}})");

	std::array<llm::Tool, 1> tools{tool};

	CHECK(provider.Generate("", tools, {}, state.ToCallbacks()).ok());

	CHECK(mock.capturedBody.find("\"tools\"") != std::string::npos);
	CHECK(mock.capturedBody.find("read_file") != std::string::npos);
}

TEST_CASE("OpenAiProvider: messages included in request body")
{
	MockHttpClient mock;
	mock.responseChunks = {
		SSE(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"),
		"data: [DONE]\n\n",
	};

	llm::OpenAiProvider provider(MakeConfig(), &mock);
	CallbackState state;

	llm::Message msg;
	msg.role = llm::Role::User;
	msg.content.push_back(llm::TextPart{"hello"});
	std::array<llm::Message, 1> history{msg};

	CHECK(provider.Generate("be helpful", {}, history, state.ToCallbacks()).ok());

	CHECK(mock.capturedBody.find("\"system\"") != std::string::npos);
	CHECK(mock.capturedBody.find("be helpful") != std::string::npos);
	CHECK(mock.capturedBody.find("\"user\"") != std::string::npos);
	CHECK(mock.capturedBody.find("hello") != std::string::npos);
}

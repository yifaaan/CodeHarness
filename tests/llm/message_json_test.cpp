#include "llm/message_json.h"

#include <doctest/doctest.h>

#include <array>
#include <nlohmann/json.hpp>

#include "llm/types.h"

namespace llm = codeharness::llm;
using json = nlohmann::json;

TEST_CASE("MessagesToJson: system prompt only") {
  auto arr = llm::MessagesToJson("You are helpful.", {});

  REQUIRE(arr.size() == 1);
  CHECK(arr[0]["role"] == "system");
  CHECK(arr[0]["content"] == "You are helpful.");
}

TEST_CASE("MessagesToJson: empty system prompt produces no system message") {
  auto arr = llm::MessagesToJson("", {});

  CHECK(arr.empty());
}

TEST_CASE("MessagesToJson: user message") {
  llm::Message msg;
  msg.role = llm::Role::kUser;
  msg.content.push_back(llm::TextPart{"Hello"});

  std::array<llm::Message, 1> msgs{msg};
  auto arr = llm::MessagesToJson("system", msgs);

  REQUIRE(arr.size() == 2);
  CHECK(arr[1]["role"] == "user");
  CHECK(arr[1]["content"] == "Hello");
}

TEST_CASE("MessagesToJson: assistant message with tool calls") {
  llm::Message msg;
  msg.role = llm::Role::kAssistant;
  msg.content.push_back(llm::TextPart{"Let me check."});
  msg.tool_calls.push_back({"call_123", "read_file", "{\"path\":\"a.txt\"}"});

  std::array<llm::Message, 1> msgs{msg};
  auto arr = llm::MessagesToJson("", msgs);

  REQUIRE(arr.size() == 1);
  CHECK(arr[0]["role"] == "assistant");
  CHECK(arr[0]["content"] == "Let me check.");
  REQUIRE(arr[0].contains("tool_calls"));
  REQUIRE(arr[0]["tool_calls"].size() == 1);
  CHECK(arr[0]["tool_calls"][0]["id"] == "call_123");
  CHECK(arr[0]["tool_calls"][0]["type"] == "function");
  CHECK(arr[0]["tool_calls"][0]["function"]["name"] == "read_file");
  CHECK(arr[0]["tool_calls"][0]["function"]["arguments"] == "{\"path\":\"a.txt\"}");
}

TEST_CASE("MessagesToJson: tool result message") {
  llm::Message msg;
  msg.role = llm::Role::kTool;
  msg.tool_call_id = "call_123";
  msg.content.push_back(llm::TextPart{"file contents here"});

  std::array<llm::Message, 1> msgs{msg};
  auto arr = llm::MessagesToJson("", msgs);

  REQUIRE(arr.size() == 1);
  CHECK(arr[0]["role"] == "tool");
  CHECK(arr[0]["tool_call_id"] == "call_123");
  CHECK(arr[0]["content"] == "file contents here");
}

TEST_CASE("MessagesToJson: multiple text parts concatenated with newline") {
  llm::Message msg;
  msg.role = llm::Role::kUser;
  msg.content.push_back(llm::TextPart{"line1"});
  msg.content.push_back(llm::TextPart{"line2"});

  std::array<llm::Message, 1> msgs{msg};
  auto arr = llm::MessagesToJson("", msgs);

  CHECK(arr[0]["content"] == "line1\nline2");
}

TEST_CASE("ToolsToJson: basic tool") {
  llm::Tool tool;
  tool.name = "read_file";
  tool.description = "Reads a file";
  tool.input_schema = json::parse(R"({"type":"object","properties":{"path":{"type":"string"}}})");

  std::array<llm::Tool, 1> tools{tool};
  auto arr = llm::ToolsToJson(tools);

  REQUIRE(arr.size() == 1);
  CHECK(arr[0]["type"] == "function");
  CHECK(arr[0]["function"]["name"] == "read_file");
  CHECK(arr[0]["function"]["description"] == "Reads a file");
  CHECK(arr[0]["function"]["parameters"]["type"] == "object");
}

TEST_CASE("ToolsToJson: null schema becomes empty object") {
  llm::Tool tool;
  tool.name = "noop";
  tool.description = "Does nothing";

  std::array<llm::Tool, 1> tools{tool};
  auto arr = llm::ToolsToJson(tools);

  CHECK(arr[0]["function"]["parameters"] == json::object());
}

TEST_CASE("ParseStreamChunk: text content delta") {
  auto chunk = llm::ParseStreamChunk(R"({"choices":[{"delta":{"content":"Hello"}}]})");

  CHECK(chunk->content == "Hello");
  CHECK_FALSE(chunk->tool_call_index.has_value());
  CHECK_FALSE(chunk->finish_reason.has_value());
  CHECK_FALSE(chunk->usage.has_value());
}

TEST_CASE("ParseStreamChunk: tool call start") {
  auto chunk = llm::ParseStreamChunk(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read_file","arguments":""}}]}}]})");

  REQUIRE(chunk->tool_call_index.has_value());
  CHECK(*chunk->tool_call_index == 0);
  CHECK(chunk->tool_call_id == "call_1");
  CHECK(chunk->tool_call_name == "read_file");
  CHECK(chunk->tool_call_args == "");
}

TEST_CASE("ParseStreamChunk: tool call argument delta") {
  auto chunk = llm::ParseStreamChunk(
      R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"path"}}]}}]})");

  REQUIRE(chunk->tool_call_index.has_value());
  CHECK(*chunk->tool_call_index == 0);
  CHECK_FALSE(chunk->tool_call_id.has_value());
  CHECK_FALSE(chunk->tool_call_name.has_value());
  CHECK(chunk->tool_call_args == "{\"path");
}

TEST_CASE("ParseStreamChunk: finish reason stop") {
  auto chunk = llm::ParseStreamChunk(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})");

  REQUIRE(chunk->finish_reason.has_value());
  CHECK(*chunk->finish_reason == "stop");
  CHECK_FALSE(chunk->content.has_value());
}

TEST_CASE("ParseStreamChunk: usage in final chunk") {
  auto chunk = llm::ParseStreamChunk(
      R"({"choices":[{"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":100,"completion_tokens":50,"total_tokens":150}})");

  REQUIRE(chunk->usage.has_value());
  CHECK(chunk->usage->output == 50);
  CHECK(chunk->usage->input_other == 100);
  CHECK(chunk->usage->input_cache_read == 0);
}

TEST_CASE("ParseStreamChunk: usage with cached tokens") {
  auto chunk = llm::ParseStreamChunk(
      R"({"choices":[],"usage":{"prompt_tokens":100,"completion_tokens":50,"total_tokens":150,"prompt_tokens_details":{"cached_tokens":80}}})");

  REQUIRE(chunk->usage.has_value());
  CHECK(chunk->usage->output == 50);
  CHECK(chunk->usage->input_other == 20);
  CHECK(chunk->usage->input_cache_read == 80);
}

TEST_CASE("ParseStreamChunk: empty choices array") {
  auto chunk = llm::ParseStreamChunk(R"({"choices":[]})");

  CHECK_FALSE(chunk->content.has_value());
  CHECK_FALSE(chunk->finish_reason.has_value());
}

TEST_CASE("ParseStreamChunk: invalid JSON returns error") {
  auto result = llm::ParseStreamChunk("not json");
  CHECK_FALSE(result.ok());
}

TEST_CASE("ParseStreamChunk: null content field") {
  auto chunk = llm::ParseStreamChunk(R"({"choices":[{"delta":{"content":null}}]})");

  CHECK_FALSE(chunk->content.has_value());
}

TEST_CASE("MapFinishReason: all mappings") {
  CHECK(llm::MapFinishReason("stop") == llm::FinishReason::kCompleted);
  CHECK(llm::MapFinishReason("tool_calls") == llm::FinishReason::kToolCalls);
  CHECK(llm::MapFinishReason("length") == llm::FinishReason::kTruncated);
  CHECK(llm::MapFinishReason("content_filter") == llm::FinishReason::kFiltered);
  CHECK(llm::MapFinishReason("unknown_thing") == llm::FinishReason::kOther);
}

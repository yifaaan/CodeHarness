#include "llm/capability.h"

#include <doctest/doctest.h>

#include "llm/types.h"

namespace llm = codeharness::llm;

TEST_CASE("Capability: gpt-4o") {
  auto cap = llm::GetCapability("gpt-4o");
  CHECK(cap.image_in);
  CHECK(cap.tool_use);
  CHECK_FALSE(cap.thinking);
  CHECK(cap.max_context_tokens == 128000);
}

TEST_CASE("Capability: gpt-4o-mini variant") {
  auto cap = llm::GetCapability("gpt-4o-mini");
  CHECK(cap.image_in);
  CHECK(cap.tool_use);
}

TEST_CASE("Capability: gpt-4.1") {
  auto cap = llm::GetCapability("gpt-4.1");
  CHECK(cap.image_in);
  CHECK(cap.tool_use);
  CHECK(cap.max_context_tokens == 1047576);
}

TEST_CASE("Capability: gpt-3.5-turbo") {
  auto cap = llm::GetCapability("gpt-3.5-turbo");
  CHECK_FALSE(cap.image_in);
  CHECK(cap.tool_use);
}

TEST_CASE("Capability: o1 reasoning model") {
  auto cap = llm::GetCapability("o1");
  CHECK(cap.thinking);
  CHECK(cap.tool_use);
  CHECK_FALSE(cap.image_in);
}

TEST_CASE("Capability: o3-mini") {
  auto cap = llm::GetCapability("o3-mini");
  CHECK(cap.thinking);
  CHECK(cap.tool_use);
}

TEST_CASE("Capability: claude-4 model") {
  auto cap = llm::GetCapability("claude-sonnet-4-20250514");
  CHECK(cap.image_in);
  CHECK(cap.thinking);
  CHECK(cap.tool_use);
}

TEST_CASE("Capability: claude-3 model") {
  auto cap = llm::GetCapability("claude-3-5-sonnet");
  CHECK(cap.image_in);
  CHECK(cap.tool_use);
  CHECK_FALSE(cap.thinking);
}

TEST_CASE("Capability: gemini-2.5") {
  auto cap = llm::GetCapability("gemini-2.5-pro");
  CHECK(cap.image_in);
  CHECK(cap.video_in);
  CHECK(cap.audio_in);
  CHECK(cap.thinking);
  CHECK(cap.tool_use);
}

TEST_CASE("Capability: unknown model returns UNKNOWN_CAPABILITY") {
  auto cap = llm::GetCapability("some-unknown-model");
  CHECK_FALSE(cap.image_in);
  CHECK_FALSE(cap.video_in);
  CHECK_FALSE(cap.audio_in);
  CHECK_FALSE(cap.thinking);
  CHECK_FALSE(cap.tool_use);
  CHECK(cap.max_context_tokens == 0);
}

TEST_CASE("Capability: case insensitive matching") {
  auto cap = llm::GetCapability("GPT-4O");
  CHECK(cap.image_in);
  CHECK(cap.tool_use);
}

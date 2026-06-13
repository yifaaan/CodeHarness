#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace codeharness::llm {

enum class Role { kUser, kAssistant, kTool };

enum class FinishReason {
  kCompleted,
  kToolCalls,
  kTruncated,
  kFiltered,
  kPaused,
  kOther,
};

enum class ThinkingEffort { kOff, kLow, kMedium, kHigh, kXHigh, kMax };

struct TextPart {
  std::string text;
};

struct ThinkPart {
  std::string think;
  std::optional<std::string> encrypted;
};

using ContentPart = std::variant<TextPart, ThinkPart>;

struct ToolCall {
  std::string id;
  std::string name;
  std::string arguments;
};

struct Message {
  Role role = Role::kUser;
  std::vector<ContentPart> content;
  std::optional<std::string> tool_call_id;
  std::vector<ToolCall> tool_calls;
};

struct Tool {
  std::string name;
  std::string description;
  nlohmann::json input_schema;
};

struct TokenUsage {
  int64_t input_other = 0;
  int64_t output = 0;
  int64_t input_cache_read = 0;
  int64_t input_cache_creation = 0;
};

struct ModelCapability {
  bool image_in = false;
  bool video_in = false;
  bool audio_in = false;
  bool thinking = false;
  bool tool_use = false;
  int64_t max_context_tokens = 0;
};

inline constexpr ModelCapability kUnknownCapability{};

}  // namespace codeharness::llm

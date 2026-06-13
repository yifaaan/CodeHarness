#pragma once

#include <absl/status/statusor.h>

#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "types.h"

namespace codeharness::llm {

nlohmann::json MessagesToJson(std::string_view system_prompt, std::span<const Message> messages);

nlohmann::json ToolsToJson(std::span<const Tool> tools);

struct StreamChunk {
  std::optional<std::string> content;
  std::optional<int> tool_call_index;
  std::optional<std::string> tool_call_id;
  std::optional<std::string> tool_call_name;
  std::optional<std::string> tool_call_args;
  std::optional<std::string> finish_reason;
  std::optional<TokenUsage> usage;
};

absl::StatusOr<StreamChunk> ParseStreamChunk(const std::string& json_str);

FinishReason MapFinishReason(std::string_view reason);

}  // namespace codeharness::llm

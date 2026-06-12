#include "codeharness/provider/provider.h"

namespace codeharness {

FinishReason FinishReasonFromString(std::string_view value) {
  if (value == "completed" || value == "stop") return FinishReason::kCompleted;
  if (value == "tool_calls" || value == "tool_use") return FinishReason::kToolCalls;
  if (value == "truncated" || value == "length") return FinishReason::kTruncated;
  if (value == "filtered" || value == "content_filter") return FinishReason::kFiltered;
  if (value == "paused") return FinishReason::kPaused;
  return FinishReason::kUnknown;
}

std::string_view FinishReasonToString(FinishReason reason) {
  switch (reason) {
    case FinishReason::kCompleted:
      return "completed";
    case FinishReason::kToolCalls:
      return "tool_calls";
    case FinishReason::kTruncated:
      return "truncated";
    case FinishReason::kFiltered:
      return "filtered";
    case FinishReason::kPaused:
      return "paused";
    case FinishReason::kUnknown:
      return "unknown";
  }
  return "unknown";
}

void to_json(nlohmann::json& output, const ProviderUsage& usage) {
  output = nlohmann::json{
      {"input_tokens", usage.input_tokens},
      {"output_tokens", usage.output_tokens},
      {"total_tokens", usage.total_tokens > 0 ? usage.total_tokens : usage.input_tokens + usage.output_tokens},
  };
}

void from_json(const nlohmann::json& input, ProviderUsage& usage) {
  usage.input_tokens = input.value("input_tokens", 0);
  usage.output_tokens = input.value("output_tokens", 0);
  usage.total_tokens = input.value("total_tokens", 0);
}

}  // namespace codeharness

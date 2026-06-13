#pragma once

#include <absl/status/status.h>

#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

#include "types.h"

namespace codeharness::llm {

struct StreamCallbacks {
  std::function<void(std::string_view)> on_text;
  std::function<void(std::string_view)> on_think;
  std::function<void(int index, std::string_view id, std::string_view name)> on_tool_call_start;
  std::function<void(int index, std::string_view args_chunk)> on_tool_call_delta;
  std::function<void(FinishReason, const TokenUsage&)> on_finish;
};

class ChatProvider {
 public:
  virtual ~ChatProvider() = default;

  virtual std::string Name() const = 0;
  virtual std::string ModelName() const = 0;
  virtual std::optional<ThinkingEffort> ThinkingEffortLevel() const = 0;

  virtual absl::Status Generate(std::string_view system_prompt, std::span<const Tool> tools,
                                std::span<const Message> history, const StreamCallbacks& callbacks,
                                std::stop_token stop_token = {}) = 0;
};

}  // namespace codeharness::llm

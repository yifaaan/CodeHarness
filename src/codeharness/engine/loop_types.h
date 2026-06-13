#pragma once

#include <functional>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "llm/types.h"
#include "tool.h"

namespace codeharness::llm {
class ChatProvider;
}

namespace codeharness::engine {

enum class StopReason {
  kCompleted,
  kMaxSteps,
  kAborted,
  kError,
};

struct StepStartedEvent {
  int step;
};

struct StepCompletedEvent {
  int step;
};

struct AssistantDeltaEvent {
  std::string text;
};

struct ToolCallStartedEvent {
  std::string id;
  std::string name;
  nlohmann::json args;
};

struct ToolResultEvent {
  std::string id;
  std::string name;
  ToolResult result;
};

struct ErrorEvent {
  std::string message;
};

using LoopEvent = std::variant<StepStartedEvent, StepCompletedEvent, AssistantDeltaEvent,
                               ToolCallStartedEvent, ToolResultEvent, ErrorEvent>;

using EventDispatcher = std::function<void(const LoopEvent&)>;

struct TurnInput {
  llm::ChatProvider* provider = nullptr;
  std::vector<ExecutableTool*> tools;
  std::string system_prompt;
  std::vector<llm::Message> history;
  EventDispatcher dispatch_event;
  std::stop_token stop_token;
  int max_steps = 1000;
};

struct TurnResult {
  StopReason stop_reason = StopReason::kCompleted;
  int steps_executed = 0;
  llm::TokenUsage total_usage;
  std::vector<llm::Message> updated_history;
  std::string error_message;
};

}  // namespace codeharness::engine

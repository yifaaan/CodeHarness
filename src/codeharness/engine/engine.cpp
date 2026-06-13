#include "codeharness/engine/engine.h"

#include <exception>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "codeharness/core/error.h"
#include "codeharness/core/event_collector.h"
#include "codeharness/core/log.h"
#include "codeharness/core/message.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/engine/loop.h"
#include "codeharness/engine/loop_types.h"
#include "codeharness/engine/tool_scheduler.h"
#include "codeharness/engine/turn_step.h"
#include "codeharness/tools/tool.h"

namespace codeharness {

namespace {

// ---------------------------------------------------------------------------
// Bridge: LoopEventDispatcher -> EngineEventSink
// ---------------------------------------------------------------------------
class EngineEventBridge final : public engine::LoopEventDispatcher {
 public:
  explicit EngineEventBridge(EngineEventSink sink) : sink_(std::move(sink)) {}

  void Recorded(const engine::LoopEvent& event) override { Forward(event); }
  void Live(const engine::LoopEvent& event) override { Forward(event); }

 private:
  void Forward(const engine::LoopEvent& event) {
    if (!sink_) return;
    std::visit(
        Overloaded{
            [this](const engine::LoopAssistantDelta& delta) { sink_(EngineAssistantTextDelta{delta.text}); },
            [this](const engine::LoopThinkingDelta&) {
              // Not forwarded to old EngineEvent.
            },
            [this](const engine::LoopToolCallStarted& started) {
              sink_(EngineToolStarted{.id = started.id, .name = started.name});
            },
            [this](const engine::LoopToolCallDelta& delta) {
              sink_(EngineToolInputDelta{.id = delta.id, .input_json_delta = delta.input_json_delta});
            },
            [this](const engine::LoopToolCallFinished& finished) { sink_(EngineToolFinished{.id = finished.id}); },
            [this](const engine::LoopToolResult& result) {
              sink_(EngineToolResult{.id = result.tool_use_id, .content = result.content, .is_error = result.is_error});
            },
            [](const engine::LoopStepStarted&) {},
            [](const engine::LoopStepEnded&) {},
        },
        event);
  }

  EngineEventSink sink_;
};

// ---------------------------------------------------------------------------
// Bridge: LoopHooks -> PermissionChecker + HookExecutor
// ---------------------------------------------------------------------------
class EngineLoopHooks final : public engine::LoopHooks {
 public:
  EngineLoopHooks(const PermissionChecker* permissions, const HookExecutor* hooks,
                  PermissionPromptHandler permission_prompt, std::vector<const Tool*> tools, std::string sender_id)
      : permissions_(permissions),
        hooks_(hooks),
        permission_prompt_(std::move(permission_prompt)),
        tools_(std::move(tools)),
        sender_id_(std::move(sender_id)) {}

  auto PrepareToolExecution(const ToolUseBlock& tool_call)
      -> absl::StatusOr<std::optional<engine::HookAction>> override {
    if (permissions_ == nullptr && hooks_ == nullptr) {
      return std::nullopt;
    }

    // Find the tool.
    const Tool* tool = engine::FindTool(tools_, tool_call.name);
    if (tool == nullptr) {
      return engine::HookAction{.blocked = true, .reason = "tool not found: " + tool_call.name};
    }

    // Parse tool input for permission target.
    ToolRequest request{.id = tool_call.id, .name = tool_call.name, .input_json = tool_call.input_json};
    auto parsed = parse_tool_request_input(request, tool_call.name);
    if (!parsed.ok()) {
      return engine::HookAction{.blocked = true, .reason = std::string{parsed.status().message()}};
    }

    // Permission check.
    if (permissions_ != nullptr) {
      const auto target = tool->permission_target(request);
      auto decision = permissions_->evaluate(tool_call.name, tool->is_read_only(), target.path, target.command);

      if (decision.action == PermissionAction::Deny) {
        spdlog::warn("tool {} denied: {}", tool_call.name, decision.reason);
        return engine::HookAction{.blocked = true, .reason = "permission denied: " + decision.reason};
      }

      if (decision.action == PermissionAction::Ask) {
        if (!permission_prompt_) {
          spdlog::warn("tool {} needs confirmation but no UI: {}", tool_call.name, decision.reason);
          return engine::HookAction{.blocked = true,
                                    .reason = "permission denied: no UI available (" + decision.reason + ")"};
        }

        auto response = permission_prompt_(PermissionPrompt{
            .id = "perm-" + tool_call.id,
            .tool_use_id = tool_call.id,
            .tool_name = tool_call.name,
            .reason = decision.reason,
            .path = target.path,
            .command = target.command,
        });
        if (!response.ok()) {
          return engine::HookAction{.blocked = true,
                                    .reason = "permission prompt failed: " + std::string{response.status().message()}};
        }
        if (!response->allowed) {
          auto reason = response->reason.empty() ? std::string{"user denied permission"} : response->reason;
          return engine::HookAction{.blocked = true, .reason = "permission denied: " + reason};
        }
      }
    }

    // Pre-tool hook.
    if (hooks_ != nullptr) {
      auto pre_result = hooks_->execute(HookEvent::PreToolUse, nlohmann::json{
                                                                   {"tool_use_id", tool_call.id},
                                                                   {"tool_name", tool_call.name},
                                                                   {"input", request.parsed_input},
                                                               });
      if (pre_result.blocked) {
        spdlog::warn("tool {} blocked by pre-tool hook: {}", tool_call.name, pre_result.reason);
        return engine::HookAction{.blocked = true, .reason = "hook blocked: " + pre_result.reason};
      }
    }

    return std::nullopt;
  }

  auto FinalizeToolResult(const ToolUseBlock& tool_call, const ToolResultBlock& result) -> absl::Status override {
    if (hooks_ == nullptr) return absl::OkStatus();

    hooks_->execute(HookEvent::PostToolUse,
                    nlohmann::json{{"tool_use_id", tool_call.id},
                                   {"tool_name", tool_call.name},
                                   {"result", {{"content", result.content}, {"is_error", result.is_error}}}});
    return absl::OkStatus();
  }

 private:
  const PermissionChecker* permissions_ = nullptr;
  const HookExecutor* hooks_ = nullptr;
  PermissionPromptHandler permission_prompt_;
  std::vector<const Tool*> tools_;
  std::string sender_id_;
};

// ---------------------------------------------------------------------------
// Tool extraction helper
// ---------------------------------------------------------------------------
auto CollectTools(ToolRegistry& registry) -> std::vector<const Tool*> {
  auto names = registry.names();
  std::vector<const Tool*> tools;
  tools.reserve(names.size());
  for (const auto& name : names) {
    tools.push_back(registry.find(name));
  }
  return tools;
}

}  // namespace

// ---------------------------------------------------------------------------
// Engine construction
// ---------------------------------------------------------------------------
Engine::Engine(ChatProvider& provider, ToolRegistry& tools, const PermissionChecker* permissions,
               const HookExecutor* hooks, PermissionPromptHandler permission_prompt, UserQuestionHandler user_question)
    : chat_provider_(&provider),
      tools_(tools),
      permissions_(permissions),
      hooks_(hooks),
      permission_prompt_(std::move(permission_prompt)),
      user_question_(std::move(user_question)) {}

auto Engine::Run(const RunRequest& request) -> absl::StatusOr<RunResult> { return RunStreaming(request, {}); }

auto Engine::RunStreaming(const RunRequest& request, const EngineEventSink& sink) -> absl::StatusOr<RunResult> {
  RunResult result;

  // -----------------------------------------------------------------------
  // 1. Build initial message list
  // -----------------------------------------------------------------------
  if (request.initial_messages) {
    result.messages = *request.initial_messages;
    if (!result.messages.empty() && result.messages.front().role == Role::kSystem) {
      result.messages.erase(result.messages.begin());
    }
  }

  if (request.system_prompt && !request.system_prompt->empty()) {
    result.messages.insert(result.messages.begin(), MakeTextMessage(Role::kSystem, *request.system_prompt));
  }

  result.messages.push_back(MakeTextMessage(Role::kUser, request.prompt));

  // -----------------------------------------------------------------------
  // 2. Set up loop infrastructure
  // -----------------------------------------------------------------------
  EngineEventBridge event_bridge(sink);

  auto tools = CollectTools(tools_);

  EngineLoopHooks loop_hooks(permissions_, hooks_, permission_prompt_, tools, sender_id_);

  // -----------------------------------------------------------------------
  // 3. Run the turn loop
  // -----------------------------------------------------------------------
  engine::TurnInput turn_input{
      .turn_id = "turn-1",
      .cancellation = request.cancellation,
      .messages = result.messages,
      .event_dispatcher = &event_bridge,
      .tools = std::move(tools),
      .hooks = &loop_hooks,
      .max_steps = request.options.max_turns > 0 ? request.options.max_turns : 1000,
  };

  auto turn_result = engine::RunTurn(turn_input, *chat_provider_);
  if (!turn_result.ok()) {
    if (absl::IsCancelled(turn_result.status())) {
      sink(EngineError{.message = "interrupted"});
    }
    return turn_result.status();
  }

  // -----------------------------------------------------------------------
  // 4. Extract result from messages
  // -----------------------------------------------------------------------
  result.messages = std::move(turn_input.messages);
  result.usage = turn_result->usage;

  // Find the last assistant message's text.
  for (auto it = result.messages.rbegin(); it != result.messages.rend(); ++it) {
    if (it->role == Role::kAssistant) {
      result.output_text = CollectText(*it);
      break;
    }
  }

  return result;
}

}  // namespace codeharness

#include "codeharness/engine/engine.h"

#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "codeharness/core/event_collector.h"
#include "codeharness/core/log.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/tools/tool.h"

namespace codeharness {

namespace {

auto emit_engine_event(const EngineEventSink& sink, EngineEvent event) -> void {
  if (sink) {
    sink(event);
  }
}

// Translate ProviderEvent values into the EngineEvent stream exposed to UI layers.
// MessageFinished and ProviderUsage are consumed by the engine and are not forwarded.
auto translate_to_engine_event(const ProviderEvent& event) -> std::optional<EngineEvent> {
  return std::visit(Overloaded{
                        [](const AssistantTextDelta& delta) -> std::optional<EngineEvent> {
                          return EngineAssistantTextDelta{delta.text};
                        },
                        [](const ToolUseStarted& started) -> std::optional<EngineEvent> {
                          return EngineToolStarted{.id = started.id, .name = started.name};
                        },
                        [](const ToolUseInputDelta& delta) -> std::optional<EngineEvent> {
                          return EngineToolInputDelta{.id = delta.id, .input_json_delta = delta.input_json_delta};
                        },
                        [](const ToolUseFinished& finished) -> std::optional<EngineEvent> {
                          return EngineToolFinished{.id = finished.id};
                        },
                        [](const MessageFinished&) -> std::optional<EngineEvent> { return std::nullopt; },
                        [](const ProviderUsage&) -> std::optional<EngineEvent> { return std::nullopt; },
                    },
                    event);
}

auto add_usage(ProviderUsage& total, const ProviderUsage& turn) -> void {
  total.input_tokens += turn.input_tokens;
  total.output_tokens += turn.output_tokens;
  total.total_tokens += turn.normalized_total();
}

// 构造一个标记为 is_error 的工具结果。execute_tool_use 里
// 多种失败（找不到工具、JSON 非法、权限拒绝、工具执行失败）都共享同一个形态。
auto make_error_tool_result(std::string id, std::string content) -> ToolResultBlock {
  return ToolResultBlock{
      .tool_use_id = std::move(id),
      .content = std::move(content),
      .is_error = true,
  };
}

auto make_unexpected_exception_tool_result(const ToolUseBlock& tool_use, std::string_view message) -> ToolResultBlock {
  return make_error_tool_result(tool_use.id,
                                "tool execution failed with an unexpected exception: " + std::string{message});
}

auto make_permission_prompt_id(const ToolUseBlock& tool_use) -> std::string { return "perm-" + tool_use.id; }

auto make_user_question_prompt_id(const ToolUseBlock& tool_use) -> std::string { return "ask-" + tool_use.id; }

auto make_user_question_tool_result(const ToolUseBlock& tool_use, const ToolRequest& request,
                                    const UserQuestionHandler& user_question) -> std::optional<ToolResultBlock> {
  if (tool_use.name != "ask_user") {
    return std::nullopt;
  }

  const auto question = request.parsed_input.value("question", std::string{});
  if (question.empty()) {
    return make_error_tool_result(tool_use.id, "ask_user requires string field: question");
  }

  if (!user_question) {
    return make_error_tool_result(tool_use.id, "user input unavailable in non-interactive mode");
  }

  auto response = user_question(UserQuestionPrompt{
      .id = make_user_question_prompt_id(tool_use),
      .tool_use_id = tool_use.id,
      .question = question,
      .reason = request.parsed_input.value("reason", std::string{}),
  });
  if (!response.ok()) {
    return make_error_tool_result(tool_use.id, "user input failed: " + std::string{response.status().message()});
  }

  return ToolResultBlock{
      .tool_use_id = tool_use.id,
      .content = response->answer,
      .is_error = false,
  };
}

auto interrupted_result() -> absl::StatusOr<RunResult> { return absl::CancelledError("interrupted"); }

auto interrupted_message() -> absl::StatusOr<Message> { return absl::CancelledError("interrupted"); }

// Returns true when the cancellation token is signalled.  When it is, emits
// an EngineError so the observer sees the interruption immediately.
auto is_cancelled(const CancellationToken& cancellation, const EngineEventSink& sink) -> bool {
  if (cancellation.is_cancelled()) {
    emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
    return true;
  }
  return false;
}

}  // namespace

Engine::Engine(Provider& provider, ToolRegistry& tools, const PermissionChecker* permissions, const HookExecutor* hooks,
               PermissionPromptHandler permission_prompt, UserQuestionHandler user_question)
    : provider_(&provider),
      tools_(tools),
      permissions_(permissions),
      hooks_(hooks),
      permission_prompt_(std::move(permission_prompt)),
      user_question_(std::move(user_question)) {}

auto Engine::run(const RunRequest& request) -> absl::StatusOr<RunResult> { return run_streaming(request, {}); }

auto Engine::run_streaming(const RunRequest& request, const EngineEventSink& sink) -> absl::StatusOr<RunResult> {
  RunResult result;

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

  for (int turn = 0; turn < request.options.max_turns; turn++) {
    if (is_cancelled(request.cancellation, sink)) return interrupted_result();

    ProviderUsage turn_usage;
    auto assistant_message = stream_provider_turn(result.messages, sink, request.cancellation, turn_usage);
    if (!assistant_message.ok()) {
      return assistant_message.status();
    }
    add_usage(result.usage, turn_usage);

    auto tool_uses = CollectToolUses(*assistant_message);
    result.messages.push_back(std::move(*assistant_message));

    if (tool_uses.empty()) {
      result.output_text = CollectText(result.messages.back());
      return result;
    }

    std::vector<ToolResultBlock> tool_results;
    tool_results.reserve(tool_uses.size());

    for (auto& tool_use : tool_uses) {
      if (is_cancelled(request.cancellation, sink)) return interrupted_result();

      const auto& permission_prompt = request.permission_prompt ? request.permission_prompt : permission_prompt_;
      const auto& user_question = request.user_question ? request.user_question : user_question_;
      auto tool_result = execute_tool_use(tool_use, permission_prompt, user_question);

      if (is_cancelled(request.cancellation, sink)) return interrupted_result();

      // Emit tool result as an event for streaming scenarios.
      emit_engine_event(sink, EngineEvent{EngineToolResult{
                                  .id = tool_result.tool_use_id,
                                  .content = tool_result.content,
                                  .is_error = tool_result.is_error,
                              }});

      tool_results.push_back(std::move(tool_result));
    }

    result.messages.push_back(MakeToolResultMessage(std::move(tool_results)));
  }

  return absl::UnavailableError("max turns exceeded");
}

auto Engine::stream_provider_turn(std::span<const Message> messages, const EngineEventSink& sink,
                                  const CancellationToken& cancellation, ProviderUsage& usage) const
    -> absl::StatusOr<Message> {
  if (is_cancelled(cancellation, sink)) return interrupted_message();

  ProviderEventCollector collector;
  collector.Message().role = Role::kAssistant;
  bool interrupted = false;

  auto streamed = provider_->stream(messages, [&](const ProviderEvent& event) {
    if (cancellation.is_cancelled()) {
      interrupted = true;
      return;
    }

    collector.OnEvent(event);
    if (const auto* updated_usage = std::get_if<ProviderUsage>(&event)) {
      usage = *updated_usage;
      usage.total_tokens = usage.normalized_total();
    }
    if (auto engine_event = translate_to_engine_event(event)) {
      emit_engine_event(sink, *engine_event);
    }

    if (cancellation.is_cancelled()) {
      interrupted = true;
    }
  });

  if (!streamed.ok()) {
    return streamed.status();
  }
  if (interrupted || cancellation.is_cancelled()) {
    emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
    return interrupted_message();
  }

  return collector.Finalize();
}

auto Engine::execute_tool_use(const ToolUseBlock& tool_use, const PermissionPromptHandler& permission_prompt,
                              const UserQuestionHandler& user_question) -> ToolResultBlock {
  try {
    ToolRequest request{
        .id = tool_use.id,
        .name = tool_use.name,
        .input_json = tool_use.input_json,
    };

    // 预解析 input_json：permission_target 与 execute 共用 parsed_input，避免重复 parse。
    if (auto parsed = parse_tool_request_input(request, tool_use.name); !parsed) {
      return make_error_tool_result(tool_use.id, std::string{parsed.status().message()});
    }

    if (auto question_result = make_user_question_tool_result(tool_use, request, user_question)) {
      return std::move(*question_result);
    }

    auto tool = tools_.find(tool_use.name);
    if (tool == nullptr) {
      return make_error_tool_result(tool_use.id, "tool not found: " + tool_use.name);
    }

    // 权限检查
    if (permissions_ != nullptr) {
      const auto target = tool->permission_target(request);
      auto decision = permissions_->evaluate(tool_use.name, tool->is_read_only(), target.path, target.command);

      // 拒绝
      if (decision.action == PermissionAction::Deny) {
        spdlog::warn("tool {} denied: {}", tool_use.name, decision.reason);
        return make_error_tool_result(tool_use.id, "permission denied: " + decision.reason);
      }

      // 权限需确认但当前没有 UI → 当成拒绝。
      // 这是有意为之：在没有交互式 UI 的环境中（如 cron、管道、
      // -p 单次模式），无法向用户请求确认，默认拒绝是最安全的降级策略。
      // 如需在无头模式下允许写操作，应配置 FullAuto 模式或将工具
      // 加入 allowed_tools 列表。调用方提供 permission_prompt 回调
      // 时（如 BackendHost / TUI），走正常的交互确认流程。
      if (decision.action == PermissionAction::Ask) {
        if (!permission_prompt) {
          spdlog::warn("tool {} needs confirmation but no UI is available: {}", tool_use.name, decision.reason);
          return make_error_tool_result(tool_use.id, "permission denied: no UI available for confirmation (" +
                                                         decision.reason + "). Use FullAuto mode or add '" +
                                                         std::string{tool_use.name} + "' to allowed_tools.");
        }

        auto response = permission_prompt(PermissionPrompt{
            .id = make_permission_prompt_id(tool_use),
            .tool_use_id = tool_use.id,
            .tool_name = tool_use.name,
            .reason = decision.reason,
            .path = target.path,
            .command = target.command,
        });
        if (!response.ok()) {
          return make_error_tool_result(tool_use.id,
                                        "permission prompt failed: " + std::string{response.status().message()});
        }

        if (!response->allowed) {
          auto reason = response->reason.empty() ? std::string{"user denied permission"} : response->reason;
          spdlog::warn("tool {} denied by user: {}", tool_use.name, reason);
          return make_error_tool_result(tool_use.id, "permission denied: " + reason);
        }
      }
    }

    if (hooks_ != nullptr) {
      const auto pre_tool_result = hooks_->execute(HookEvent::PreToolUse, nlohmann::json{
                                                                              {"tool_use_id", tool_use.id},
                                                                              {"tool_name", tool_use.name},
                                                                              {"input", request.parsed_input},
                                                                          });
      if (pre_tool_result.blocked) {
        spdlog::warn("tool {} blocked by pre-tool hook: {}", tool_use.name, pre_tool_result.reason);
        return make_error_tool_result(tool_use.id, "hook blocked tool execution: " + pre_tool_result.reason);
      }
    }

    // 执行工具
    ToolContext context;
    context.cwd = std::filesystem::current_path();
    if (!sender_id_.empty()) {
      context.sender_id = sender_id_;
    }

    spdlog::info("tool {} starting (id={})", tool_use.name, tool_use.id);
    auto response = tool->execute(request, context);
    if (!response.ok()) {
      spdlog::warn("tool {} failed: {}", tool_use.name, response.status().message());
      return make_error_tool_result(tool_use.id, std::string{response.status().message()});
    }
    spdlog::info("tool {} done (is_error={})", tool_use.name, response->is_error);

    if (hooks_ != nullptr) {
      const auto post_tool_result = hooks_->execute(HookEvent::PostToolUse, nlohmann::json{
                                                                                {"tool_use_id", tool_use.id},
                                                                                {"tool_name", tool_use.name},
                                                                                {"input", request.parsed_input},
                                                                                {"result",
                                                                                 {
                                                                                     {"content", response->content},
                                                                                     {"is_error", response->is_error},
                                                                                 }},
                                                                            });
      if (post_tool_result.blocked) {
        spdlog::warn("post-tool hook blocked tool result for {}: {}", tool_use.name, post_tool_result.reason);
        return make_error_tool_result(tool_use.id, "hook blocked tool result: " + post_tool_result.reason);
      }
    }

    return ToolResultBlock{
        .tool_use_id = response->tool_use_id,
        .content = response->content,
        .is_error = response->is_error,
    };
  } catch (const std::exception& error) {
    spdlog::warn("tool {} raised an unexpected exception: {}", tool_use.name, error.what());
    return make_unexpected_exception_tool_result(tool_use, error.what());
  } catch (...) {
    spdlog::warn("tool {} raised an unknown exception", tool_use.name);
    return make_unexpected_exception_tool_result(tool_use, "unknown exception");
  }
}

}  // namespace codeharness

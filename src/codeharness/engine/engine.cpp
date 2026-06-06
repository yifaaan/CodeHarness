#include "codeharness/engine/engine.h"

#include <nonstd/expected.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "codeharness/core/event_collector.h"
#include "codeharness/core/log.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/tools/tool.h"

namespace codeharness
{

namespace
{

auto emit_engine_event(const EngineEventSink& sink, EngineEvent event) -> void
{
    if (sink)
    {
        sink(event);
    }
}

// 把 ProviderEvent 翻译成对应的 EngineEvent。
// ToolUseInputDelta / MessageFinished 没有对应的引擎事件，返回 nullopt。
auto translate_to_engine_event(const ProviderEvent& event) -> std::optional<EngineEvent>
{
    return std::visit(
        Overloaded{
            [](const AssistantTextDelta& delta) -> std::optional<EngineEvent> {
                return EngineAssistantTextDelta{delta.text};
            },
            [](const ToolUseStarted& started) -> std::optional<EngineEvent> {
                return EngineToolStarted{.id = started.id, .name = started.name};
            },
            [](const ToolUseInputDelta&) -> std::optional<EngineEvent> { return std::nullopt; },
            [](const ToolUseFinished& finished) -> std::optional<EngineEvent> {
                return EngineToolFinished{.id = finished.id};
            },
            [](const MessageFinished&) -> std::optional<EngineEvent> { return std::nullopt; },
        },
        event);
}

// 构造一个标记为 is_error 的工具结果。execute_tool_use 里
// 多种失败（找不到工具、JSON 非法、权限拒绝、工具执行失败）都共享同一个形态。
auto make_error_tool_result(std::string id, std::string content) -> ToolResultBlock
{
    return ToolResultBlock{
        .tool_use_id = std::move(id),
        .content = std::move(content),
        .is_error = true,
    };
}

auto make_permission_prompt_id(const ToolUseBlock& tool_use) -> std::string
{
    return "perm-" + tool_use.id;
}

auto interrupted_result() -> Result<RunResult>
{
    return fail<RunResult>(ErrorKind::Cancelled, "interrupted");
}

auto interrupted_message() -> Result<Message>
{
    return fail<Message>(ErrorKind::Cancelled, "interrupted");
}

} // namespace

Engine::Engine(
    Provider& provider,
    ToolRegistry& tools,
    const PermissionChecker* permissions,
    const HookExecutor* hooks,
    PermissionPromptHandler permission_prompt) :
    provider_(provider),
    tools_(tools),
    permissions_(permissions),
    hooks_(hooks),
    permission_prompt_(std::move(permission_prompt))
{
}

auto Engine::run(const RunRequest& request) -> Result<RunResult>
{
    return run_streaming(request, {});
}

auto Engine::run_streaming(const RunRequest& request, const EngineEventSink& sink) -> Result<RunResult>
{
    RunResult result;

    if (request.initial_messages)
    {
        result.messages = *request.initial_messages;
        if (!result.messages.empty() && result.messages.front().role == Role::System)
        {
            result.messages.erase(result.messages.begin());
        }
    }

    if (request.system_prompt && !request.system_prompt->empty())
    {
        result.messages.insert(result.messages.begin(), make_text_message(Role::System, *request.system_prompt));
    }

    result.messages.push_back(make_text_message(Role::User, request.prompt));

    for (int turn = 0; turn < request.options.max_turns; turn++)
    {
        if (request.cancellation.is_cancelled())
        {
            emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
            return interrupted_result();
        }

        auto assistant_message = stream_provider_turn(result.messages, sink, request.cancellation);
        if (!assistant_message)
        {
            return nonstd::make_unexpected(assistant_message.error());
        }

        auto tool_uses = collect_tool_uses(*assistant_message);
        result.messages.push_back(std::move(*assistant_message));

        if (tool_uses.empty())
        {
            result.output_text = collect_text(result.messages.back());
            return result;
        }

        std::vector<ToolResultBlock> tool_results;
        tool_results.reserve(tool_uses.size());

        for (auto& tool_use : tool_uses)
        {
            if (request.cancellation.is_cancelled())
            {
                emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
                return interrupted_result();
            }

            const auto& permission_prompt = request.permission_prompt ? request.permission_prompt : permission_prompt_;
            auto tool_result = execute_tool_use(tool_use, permission_prompt);

            if (request.cancellation.is_cancelled())
            {
                emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
                return interrupted_result();
            }

            // Emit tool result as an event for streaming scenarios.
            emit_engine_event(
                sink,
                EngineEvent{EngineToolResult{
                    .id = tool_result.tool_use_id,
                    .content = tool_result.content,
                    .is_error = tool_result.is_error,
                }});

            tool_results.push_back(std::move(tool_result));
        }

        result.messages.push_back(make_tool_result_message(std::move(tool_results)));
    }

    return fail<RunResult>(ErrorKind::Provider, "max turns exceeded");
}

auto Engine::stream_provider_turn(std::span<const Message> messages,
                                  const EngineEventSink& sink,
                                  const CancellationToken& cancellation) const
    -> Result<Message>
{
    if (cancellation.is_cancelled())
    {
        emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
        return interrupted_message();
    }

    ProviderEventCollector collector;
    collector.message().role = Role::Assistant;
    bool interrupted = false;

    auto streamed = provider_.stream(messages, [&](const ProviderEvent& event) {
        if (cancellation.is_cancelled())
        {
            interrupted = true;
            return;
        }

        collector.on_event(event);
        if (auto engine_event = translate_to_engine_event(event))
        {
            emit_engine_event(sink, *engine_event);
        }

        if (cancellation.is_cancelled())
        {
            interrupted = true;
        }
    });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }
    if (interrupted || cancellation.is_cancelled())
    {
        emit_engine_event(sink, EngineEvent{EngineError{.message = "interrupted"}});
        return interrupted_message();
    }

    return collector.finalize();
}

auto Engine::execute_tool_use(const ToolUseBlock& tool_use, const PermissionPromptHandler& permission_prompt)
    -> ToolResultBlock
{
    auto tool = tools_.find(tool_use.name);
    if (tool == nullptr)
    {
        return make_error_tool_result(tool_use.id, "tool not found: " + tool_use.name);
    }

    ToolRequest request{
        .id = tool_use.id,
        .name = tool_use.name,
        .input_json = tool_use.input_json,
    };

    // 预解析 input_json：permission_target 与 execute 共用 parsed_input，避免重复 parse。
    if (auto parsed = parse_tool_request_input(request, tool_use.name); !parsed)
    {
        return make_error_tool_result(tool_use.id, std::string{parsed.error().message});
    }

    // 权限检查
    if (permissions_ != nullptr)
    {
        const auto target = tool->permission_target(request);
        auto decision = permissions_->evaluate(tool_use.name, tool->is_read_only(), target.path, target.command);

        // 拒绝
        if (decision.action == PermissionAction::Deny)
        {
            spdlog::warn("tool {} denied: {}", tool_use.name, decision.reason);
            return make_error_tool_result(tool_use.id, "permission denied: " + decision.reason);
        }

        // TODO: 需要确认但当前没有 UI → 当成拒绝。
        if (decision.action == PermissionAction::Ask)
        {
            if (!permission_prompt)
            {
                spdlog::warn("tool {} needs confirmation but no prompt: {}", tool_use.name, decision.reason);
                return make_error_tool_result(
                    tool_use.id, "permission confirmation required but no prompt is configured: " + decision.reason);
            }

            auto response = permission_prompt(PermissionPrompt{
                .id = make_permission_prompt_id(tool_use),
                .tool_use_id = tool_use.id,
                .tool_name = tool_use.name,
                .reason = decision.reason,
                .path = target.path,
                .command = target.command,
            });
            if (!response)
            {
                return make_error_tool_result(tool_use.id, "permission prompt failed: " + response.error().message);
            }

            if (!response->allowed)
            {
                auto reason = response->reason.empty() ? std::string{"user denied permission"} : response->reason;
                spdlog::warn("tool {} denied by user: {}", tool_use.name, reason);
                return make_error_tool_result(tool_use.id, "permission denied: " + reason);
            }
        }
    }

    if (hooks_ != nullptr)
    {
        const auto pre_tool_result = hooks_->execute(
            HookEvent::PreToolUse,
            nlohmann::json{
                {"tool_use_id", tool_use.id},
                {"tool_name", tool_use.name},
                {"input", request.parsed_input},
            });
        if (pre_tool_result.blocked)
        {
            spdlog::warn("tool {} blocked by pre-tool hook: {}", tool_use.name, pre_tool_result.reason);
            return make_error_tool_result(tool_use.id, "hook blocked tool execution: " + pre_tool_result.reason);
        }
    }

    // 执行工具
    ToolContext context;
    context.cwd = std::filesystem::current_path();

    spdlog::info("tool {} starting (id={})", tool_use.name, tool_use.id);
    auto response = tool->execute(request, context);
    if (!response)
    {
        spdlog::warn("tool {} failed: {}", tool_use.name, response.error().message);
        return make_error_tool_result(tool_use.id, std::string{response.error().message});
    }
    spdlog::info("tool {} done (is_error={})", tool_use.name, response->is_error);

    if (hooks_ != nullptr)
    {
        const auto post_tool_result = hooks_->execute(
            HookEvent::PostToolUse,
            nlohmann::json{
                {"tool_use_id", tool_use.id},
                {"tool_name", tool_use.name},
                {"input", request.parsed_input},
                {"result",
                 {
                     {"content", response->content},
                     {"is_error", response->is_error},
                 }},
            });
        if (post_tool_result.blocked)
        {
            spdlog::warn("post-tool hook blocked tool result for {}: {}", tool_use.name, post_tool_result.reason);
            return make_error_tool_result(tool_use.id, "hook blocked tool result: " + post_tool_result.reason);
        }
    }

    return ToolResultBlock{
        .tool_use_id = response->tool_use_id,
        .content = response->content,
        .is_error = response->is_error,
    };
}

} // namespace codeharness

#include "codeharness/engine/engine.h"

#include <nonstd/expected.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "codeharness/core/event_collector.h"
#include "codeharness/core/overloaded.h"

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

} // namespace

Engine::Engine(Provider& provider) : provider_(provider)
{
}

Engine::Engine(Provider& provider, ToolRegistry& tools) : provider_(provider), tools_(&tools)
{
}

Engine::Engine(Provider& provider, ToolRegistry& tools, const PermissionChecker& permissions) :
    provider_(provider), tools_(&tools), permissions_(&permissions)
{
}

auto Engine::run(const RunRequest& request) -> Result<RunResult>
{
    return run_streaming(request, {});
}

auto Engine::run_streaming(const RunRequest& request, const EngineEventSink& sink) -> Result<RunResult>
{
    RunResult result;
    result.messages.push_back(make_text_message(Role::User, request.prompt));

    for (int turn = 0; turn < request.options.max_turns; turn++)
    {
        auto assistant_message = stream_provider_turn(result.messages, sink);
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
            auto tool_result = execute_tool_use(tool_use);

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

auto Engine::stream_provider_turn(std::span<const Message> messages, const EngineEventSink& sink) const
    -> Result<Message>
{
    ProviderEventCollector collector;
    collector.message().role = Role::Assistant;

    auto streamed = provider_.stream(messages, [&](const ProviderEvent& event) {
        collector.on_event(event);
        if (auto engine_event = translate_to_engine_event(event))
        {
            emit_engine_event(sink, *engine_event);
        }
    });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }

    return collector.finalize();
}

auto Engine::execute_tool_use(const ToolUseBlock& tool_use) -> ToolResultBlock
{
    auto tool = tools_->find(tool_use.name);
    if (tool == nullptr)
    {
        return ToolResultBlock{
            .tool_use_id = tool_use.id,
            .content = "tool not found: " + tool_use.name,
            .is_error = true,
        };
    }

    ToolRequest request{
        .id = tool_use.id,
        .name = tool_use.name,
        .input_json = tool_use.input_json,
    };

    // 权限检查
    if (permissions_ != nullptr)
    {
        const auto target = tool->permission_target(request);
        auto decision = permissions_->evaluate(tool_use.name, tool->is_read_only(), target.path, target.command);

        // 拒绝
        if (decision.action == PermissionAction::Deny)
        {
            return ToolResultBlock{
                .tool_use_id = tool_use.id,
                .content = "permission denied: " + decision.reason,
                .is_error = true,
            };
        }

        // TODO: 需要确认但当前没有 UI → 当成拒绝。
        if (decision.action == PermissionAction::Ask)
        {
            return ToolResultBlock{
                .tool_use_id = tool_use.id,
                .content = "permission confirmation required but no prompt is configured: " + decision.reason,
                .is_error = true,
            };
        }
    }

    // 执行工具
    ToolContext context;
    context.cwd = std::filesystem::current_path();

    auto response = tool->execute(request, context);
    if (!response)
    {
        return ToolResultBlock{
            .tool_use_id = tool_use.id,
            .content = response.error().message,
            .is_error = true,
        };
    }

    return ToolResultBlock{
        .tool_use_id = response->tool_use_id,
        .content = response->content,
        .is_error = response->is_error,
    };
}

} // namespace codeharness

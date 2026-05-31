#include "codeharness/engine/engine.h"

#include <nonstd/expected.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace codeharness
{

namespace
{

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

auto emit_engine_event(const EngineEventSink& sink, EngineEvent event) -> void
{
    if (sink)
    {
        sink(event);
    }
}

} // namespace

Engine::Engine(Provider& provider) : provider_(provider)
{
}

Engine::Engine(Provider& provider, const ToolRegistry& tools) : provider_(provider), tools_(&tools)
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
    Message message;
    message.role = Role::Assistant;

    std::unordered_map<std::string, std::size_t> tool_block_by_id;
    std::optional<CodeHarnessError> event_error;

    auto set_event_error = [&](ErrorKind kind, std::string text) {
        if (!event_error)
        {
            event_error = CodeHarnessError{kind, std::move(text)};
        }
    };

    auto streamed = provider_.stream(messages, [&](const ProviderEvent& event) {
        std::visit(
            Overloaded{
                [&](const AssistantTextDelta& delta) {
                    message.content.emplace_back(TextBlock{delta.text});

                    emit_engine_event(sink, EngineAssistantTextDelta{delta.text});
                },
                [&](const ToolUseStarted& started) {
                    if (tool_block_by_id.contains(started.id))
                    {
                        set_event_error(ErrorKind::Provider, "duplicate tool use id: " + started.id);
                        return;
                    }

                    tool_block_by_id[started.id] = message.content.size();
                    message.content.emplace_back(
                        ToolUseBlock{.id = started.id, .name = started.name, .input_json = ""});

                    emit_engine_event(
                        sink,
                        EngineToolStarted{
                            .id = started.id,
                            .name = started.name,
                        });
                },
                [&](const ToolUseInputDelta& delta) {
                    auto found = tool_block_by_id.find(delta.id);
                    if (found == tool_block_by_id.end())
                    {
                        set_event_error(ErrorKind::Provider, "tool input delta before tool start: " + delta.id);
                        return;
                    }

                    auto tool_use = std::get_if<ToolUseBlock>(&message.content[found->second]);
                    if (tool_use == nullptr)
                    {
                        set_event_error(ErrorKind::Internal, "tool block type mismatch");
                        return;
                    }

                    tool_use->input_json += delta.input_json_delta;
                },
                [&](const ToolUseFinished& finished) {
                    if (!tool_block_by_id.contains(finished.id))
                    {
                        set_event_error(ErrorKind::Provider, "tool finished before tool start: " + finished.id);
                        return;
                    }

                    emit_engine_event(
                        sink,
                        EngineToolFinished{
                            .id = finished.id,
                        });
                },
                [&](const MessageFinished&) {}},
            event);
    });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }

    if (event_error)
    {
        return nonstd::make_unexpected(*event_error);
    }

    return message;
}

auto Engine::execute_tool_use(const ToolUseBlock& tool_use) const -> ToolResultBlock
{
    ToolRequest request{.id = tool_use.id, .name = tool_use.name, .input_json = tool_use.input_json};

    ToolContext context;
    context.cwd = std::filesystem::current_path();

    auto response = tools_->execute(request, context);
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

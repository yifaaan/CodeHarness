#include "codeharness/ui_backend/ui_backend.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/overloaded.h"

#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <istream>
#include <ostream>
#include <string>
#include <utility>

namespace codeharness::ui_backend
{

namespace
{

constexpr std::string_view BACKEND_EVENT_PREFIX = "OHJSON:";

auto prefixed_json_line(nlohmann::json event) -> std::string
{
    return std::string{BACKEND_EVENT_PREFIX} + event.dump() + '\n';
}

auto engine_event_to_backend_event(const EngineEvent& event) -> BackendEvent
{
    return std::visit(
        Overloaded{
            [](const EngineAssistantTextDelta& delta) -> BackendEvent {
                return BackendAssistantDelta{.text = delta.text};
            },
            [](const EngineToolStarted& started) -> BackendEvent {
                return BackendToolStarted{.id = started.id, .name = started.name};
            },
            [](const EngineToolFinished& finished) -> BackendEvent {
                return BackendToolCompleted{.id = finished.id};
            },
            [](const EngineToolResult& result) -> BackendEvent {
                return BackendToolResult{
                    .id = result.id,
                    .content = result.content,
                    .is_error = result.is_error,
                };
            },
            [](const EngineError& error) -> BackendEvent {
                return BackendError{.message = error.message};
            },
        },
        event);
}

auto is_submit_line(const FrontendRequest& request) -> bool
{
    return request.type == "submit_line";
}

auto is_shutdown(const FrontendRequest& request) -> bool
{
    return request.type == "shutdown";
}

} // namespace

auto parse_frontend_request(std::string_view line) -> Result<FrontendRequest>
{
    nlohmann::json input;
    try
    {
        input = nlohmann::json::parse(line);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<FrontendRequest>(
            ErrorKind::InvalidArgument,
            std::string{"failed to parse frontend request: "} + error.what());
    }

    if (!input.is_object())
    {
        return fail<FrontendRequest>(ErrorKind::InvalidArgument, "frontend request must be a JSON object");
    }

    auto type = read_json_field<std::string>(input, "type", "frontend request");
    if (!type)
    {
        return nonstd::make_unexpected(type.error());
    }

    auto request_line = read_optional_json_field<std::string>(input, "line", "frontend request");
    if (!request_line)
    {
        return nonstd::make_unexpected(request_line.error());
    }

    return FrontendRequest{
        .type = std::move(*type),
        .line = std::move(*request_line),
    };
}

auto format_backend_event(const BackendEvent& event) -> std::string
{
    return std::visit(
        Overloaded{
            [](const BackendReady&) {
                return prefixed_json_line(nlohmann::json{{"type", "ready"}});
            },
            [](const BackendAssistantDelta& delta) {
                return prefixed_json_line(nlohmann::json{{"type", "assistant_delta"}, {"text", delta.text}});
            },
            [](const BackendToolStarted& started) {
                return prefixed_json_line(
                    nlohmann::json{{"type", "tool_started"}, {"id", started.id}, {"name", started.name}});
            },
            [](const BackendToolCompleted& completed) {
                return prefixed_json_line(nlohmann::json{{"type", "tool_completed"}, {"id", completed.id}});
            },
            [](const BackendToolResult& result) {
                return prefixed_json_line(
                    nlohmann::json{
                        {"type", "tool_result"},
                        {"id", result.id},
                        {"content", result.content},
                        {"is_error", result.is_error},
                    });
            },
            [](const BackendLineComplete&) {
                return prefixed_json_line(nlohmann::json{{"type", "line_complete"}});
            },
            [](const BackendError& error) {
                return prefixed_json_line(nlohmann::json{{"type", "error"}, {"message", error.message}});
            },
            [](const BackendShutdown&) {
                return prefixed_json_line(nlohmann::json{{"type", "shutdown"}});
            },
        },
        event);
}

BackendHost::BackendHost(runtime::RuntimeBundle& runtime, std::istream& input, std::ostream& output, int max_turns) :
    runtime_{runtime}, input_{input}, output_{output}, max_turns_{max_turns}
{
}

auto BackendHost::run() -> Result<void>
{
    emit(BackendReady{});

    std::string line;
    while (std::getline(input_, line))
    {
        auto request = parse_frontend_request(line);
        if (!request)
        {
            emit(BackendError{.message = request.error().message});
            continue;
        }

        if (!handle_request(*request))
        {
            break;
        }
    }

    return {};
}

auto BackendHost::emit(const BackendEvent& event) -> void
{
    output_ << format_backend_event(event);
}

auto BackendHost::handle_request(const FrontendRequest& request) -> bool
{
    if (is_shutdown(request))
    {
        emit(BackendShutdown{});
        return false;
    }

    if (!is_submit_line(request))
    {
        emit(BackendError{.message = "unsupported frontend request type: " + request.type});
        return true;
    }

    if (!request.line || request.line->empty())
    {
        emit(BackendError{.message = "submit_line requires non-empty line"});
        return true;
    }

    handle_submit_line(*request.line);
    return true;
}

auto BackendHost::handle_submit_line(std::string_view line) -> void
{
    auto prompt = std::string{line};

    if (!prompt.empty() && prompt.front() == '/')
    {
        auto command_result = execute_slash_command(runtime_.commands(), prompt);
        if (!command_result)
        {
            emit(BackendError{.message = command_result.error().message});
            return;
        }

        if (command_result->message)
        {
            emit(BackendAssistantDelta{.text = *command_result->message});
        }

        if (!command_result->submit_prompt)
        {
            emit(BackendLineComplete{});
            return;
        }

        prompt = *command_result->submit_prompt;
    }

    auto result = runtime_.run_prompt(prompt, max_turns_, [this](const EngineEvent& event) {
        emit_engine_event(event);
    });
    if (!result)
    {
        emit(BackendError{.message = result.error().message});
        return;
    }

    emit(BackendLineComplete{});
}

auto BackendHost::emit_engine_event(const EngineEvent& event) -> void
{
    emit(engine_event_to_backend_event(event));
}

} // namespace codeharness::ui_backend

#include "codeharness/ui_backend/ui_backend.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/core/strings.h"

#include <nlohmann/json.hpp>
#include <algorithm>
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
            [](const EngineToolInputDelta& delta) -> BackendEvent {
                return BackendToolInputDelta{.id = delta.id, .input_json_delta = delta.input_json_delta};
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

auto command_invocation_to_string(CommandInvocationKind invocation) -> std::string
{
    switch (invocation)
    {
    case CommandInvocationKind::MessageOnly:
        return "message_only";
    case CommandInvocationKind::SubmitsPrompt:
        return "submits_prompt";
    case CommandInvocationKind::Unknown:
        return "unknown";
    }

    return "unknown";
}

auto command_matches_query(const SlashCommand& command, std::string_view query) -> bool
{
    if (query.empty())
    {
        return true;
    }

    if (command.name.find(query) != std::string::npos || command.description.find(query) != std::string::npos)
    {
        return true;
    }

    return std::ranges::any_of(command.aliases, [query](const auto& alias) {
        return alias.find(query) != std::string::npos;
    });
}

auto make_select_request(const CommandRegistry& registry, std::string_view query) -> BackendSelectRequest
{
    BackendSelectRequest request;
    for (const auto& command : registry.list())
    {
        if (!command_matches_query(command, query))
        {
            continue;
        }

        request.commands.push_back(
            BackendCommandEntry{
                .name = command.name,
                .description = command.description,
                .aliases = command.aliases,
                .invocation = command_invocation_to_string(command.invocation),
            });
    }

    return request;
}

auto format_command_line(std::string_view command, std::string_view args) -> std::string
{
    auto line = std::string{command};
    if (!line.starts_with('/'))
    {
        line.insert(line.begin(), '/');
    }
    if (!args.empty())
    {
        line += ' ';
        line += args;
    }
    return line;
}

auto is_runtime_permission_command(std::string_view prompt) -> bool
{
    const auto trimmed = Trim(prompt);
    return trimmed == "/plan" || trimmed == "/plan on" || trimmed == "/plan enter" ||
           trimmed == "/act" || trimmed == "/plan off" || trimmed == "/plan exit" ||
           trimmed == "/fullauto" || trimmed == "/full_auto" || trimmed == "/permissions full_auto" ||
           trimmed == "/default" || trimmed == "/permissions default" ||
           trimmed == "/mode" || trimmed == "/permissions";
}

} // namespace

auto parse_frontend_request(std::string_view line) -> absl::StatusOr<FrontendRequest>
{
    nlohmann::json input;
    try
    {
        input = nlohmann::json::parse(line);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return absl::StatusOr<FrontendRequest>(absl::InvalidArgumentError(std::string{"failed to parse frontend request: "} + error.what()));
    }

    if (!input.is_object())
    {
        return absl::StatusOr<FrontendRequest>(absl::InvalidArgumentError("frontend request must be a JSON object"));
    }

    auto type = ReadJsonField<std::string>(input, "type", "frontend request");
    if (!type)
    {
        return type.error();
    }

    auto request_line = ReadOptionalJsonField<std::string>(input, "line", "frontend request");
    if (!request_line)
    {
        return request_line.error();
    }
    auto request_id = ReadOptionalJsonField<std::string>(input, "request_id", "frontend request");
    if (!request_id)
    {
        return request_id.error();
    }
    auto allowed = ReadOptionalJsonField<bool>(input, "allowed", "frontend request");
    if (!allowed)
    {
        return allowed.error();
    }
    auto remember_session = ReadOptionalJsonField<bool>(input, "remember_session", "frontend request");
    if (!remember_session)
    {
        return remember_session.error();
    }
    auto command = ReadOptionalJsonField<std::string>(input, "command", "frontend request");
    if(!command.ok())
    {
        return command.status();
    }
    auto args = ReadOptionalJsonField<std::string>(input, "args", "frontend request");
    if (!args)
    {
        return args.error();
    }
    auto query = ReadOptionalJsonField<std::string>(input, "query", "frontend request");
    if (!query)
    {
        return query.error();
    }
    auto answer = ReadOptionalJsonField<std::string>(input, "answer", "frontend request");
    if (!answer)
    {
        return answer.error();
    }
    auto profile_id = ReadOptionalJsonField<std::string>(input, "profile_id", "frontend request");
    if (!profile_id)
    {
        return profile_id.error();
    }

    return FrontendRequest{
        .type = std::move(*type),
        .line = std::move(*request_line),
        .request_id = std::move(*request_id),
        .allowed = std::move(*allowed),
        .remember_session = std::move(*remember_session),
        .command = std::move(*command),
        .args = std::move(*args),
        .query = std::move(*query),
        .answer = std::move(*answer),
        .profile_id = std::move(*profile_id),
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
            [](const BackendToolInputDelta& delta) {
                return prefixed_json_line(
                    nlohmann::json{
                        {"type", "tool_input_delta"},
                        {"id", delta.id},
                        {"input_json_delta", delta.input_json_delta},
                    });
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
            [](const BackendPermissionModal& modal) {
                nlohmann::json payload{
                    {"kind", "permission"},
                    {"request_id", modal.id},
                    {"tool_use_id", modal.tool_use_id},
                    {"tool_name", modal.tool_name},
                    {"reason", modal.reason},
                };
                if (modal.path)
                {
                    payload["path"] = *modal.path;
                }
                if (modal.command)
                {
                    payload["command"] = *modal.command;
                }
                return prefixed_json_line(nlohmann::json{{"type", "modal_request"}, {"modal", std::move(payload)}});
            },
            [](const BackendUserQuestionModal& modal) {
                return prefixed_json_line(
                    nlohmann::json{
                        {"type", "modal_request"},
                        {"modal",
                         {
                             {"kind", "ask_user"},
                             {"request_id", modal.id},
                             {"tool_use_id", modal.tool_use_id},
                             {"question", modal.question},
                             {"reason", modal.reason},
                         }},
                    });
            },
            [](const BackendSelectRequest& select) {
                nlohmann::json commands = nlohmann::json::array();
                for (const auto& command : select.commands)
                {
                    commands.push_back(
                        nlohmann::json{
                            {"name", command.name},
                            {"description", command.description},
                            {"aliases", command.aliases},
                            {"invocation", command.invocation},
                        });
                }
                return prefixed_json_line(nlohmann::json{{"type", "select_request"}, {"commands", std::move(commands)}});
            },
            [](const BackendModelSelectRequest& select) {
                nlohmann::json profiles = nlohmann::json::array();
                for (const auto& profile : select.profiles)
                {
                    profiles.push_back(
                        nlohmann::json{
                            {"id", profile.id},
                            {"label", profile.label},
                            {"description", profile.description},
                            {"is_current", profile.is_current},
                        });
                }
                return prefixed_json_line(nlohmann::json{{"type", "model_select"}, {"profiles", std::move(profiles)}});
            },
            [](const BackendLineComplete&) {
                return prefixed_json_line(nlohmann::json{{"type", "line_complete"}});
            },
            [](const BackendUsage& usage) {
                return prefixed_json_line(
                    nlohmann::json{
                        {"type", "usage"},
                        {"input_tokens", usage.input_tokens},
                        {"output_tokens", usage.output_tokens},
                        {"total_tokens", usage.total_tokens},
                    });
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

auto BackendHost::run() -> absl::Status
{
    emit(BackendReady{});

    while (true)
    {
        reap_finished_run();
        auto request = next_frontend_request();
        if (!request)
        {
            emit(BackendError{.message = request.status().message()});
            continue;
        }
        if (!*request)
        {
            if (has_active_run())
            {
                wait_for_active_run();
                if (!queued_requests_.empty())
                {
                    continue;
                }
            }
            break;
        }

        if (!handle_request(**request))
        {
            break;
        }
    }

    return {};
}

auto BackendHost::emit(const BackendEvent& event) -> void
{
    std::lock_guard lock{output_mutex_};
    output_ << format_backend_event(event);
}

auto BackendHost::handle_request(const FrontendRequest& request) -> bool
{
    reap_finished_run();

    if (request.type == "shutdown")
    {
        if (has_active_run())
        {
            handle_interrupt();
            wait_for_active_run();
        }
        emit(BackendShutdown{});
        return false;
    }

    if (request.type == "permission_response")
    {
        handle_permission_response(request);
        return true;
    }

    if (request.type == "user_question_response")
    {
        handle_user_question_response(request);
        return true;
    }

    if (request.type == "interrupt")
    {
        handle_interrupt();
        return true;
    }

    if (has_active_run())
    {
        if (request.type == "submit_line")
        {
            queued_requests_.push(request);
            return true;
        }

        emit(BackendError{.message = "run already in progress"});
        return true;
    }

    if (request.type == "select_command")
    {
        handle_select_command(request);
        return true;
    }

    if (request.type == "apply_select_command")
    {
        handle_apply_select_command(request);
        return true;
    }

    if (request.type == "select_model")
    {
        handle_select_model();
        return true;
    }

    if (request.type == "apply_model")
    {
        handle_apply_model(request);
        return true;
    }

    if (request.type != "submit_line")
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

auto BackendHost::next_frontend_request() -> absl::StatusOr<std::optional<FrontendRequest>>
{
    if (!queued_requests_.empty())
    {
        auto request = std::move(queued_requests_.front());
        queued_requests_.pop();
        return request;
    }

    std::string line;
    if (!std::getline(input_, line))
    {
        return std::optional<FrontendRequest>{};
    }

    auto request = parse_frontend_request(line);
    if (!request)
    {
        return request.error();
    }

    return std::move(*request);
}

auto BackendHost::handle_submit_line(std::string line) -> void
{
    if (has_active_run())
    {
        emit(BackendError{.message = "run already in progress"});
        return;
    }

    {
        std::lock_guard lock{state_mutex_};
        active_cancellation_ = std::make_unique<CancellationSource>();
        active_run_ = true;
        active_run_finished_ = false;
        pending_permission_.reset();
        pending_permission_response_.reset();
        pending_user_question_.reset();
        pending_user_question_response_.reset();
    }

    active_worker_ = std::thread{[this, line = std::move(line)]() mutable {
        run_submit_line(std::move(line));
        {
            std::lock_guard lock{state_mutex_};
            active_run_finished_ = true;
            active_run_ = false;
            pending_permission_.reset();
            pending_permission_response_.reset();
            pending_user_question_.reset();
            pending_user_question_response_.reset();
        }
        permission_cv_.notify_all();
        user_question_cv_.notify_all();
    }};
}

auto BackendHost::run_submit_line(std::string line) -> void
{
    auto prompt = std::move(line);

    if (!prompt.empty() && prompt.front() == '/')
    {
        if (is_runtime_permission_command(prompt))
        {
            auto result = runtime_.run_prompt(prompt, runtime::RunPromptOptions{.max_turns = max_turns_}, [this](const EngineEvent& event) {
                emit_engine_event(event);
            });
            if(!result.ok())
            {
                emit(BackendError{.message = result.status().message()});
            }
            else if (!result->output_text.empty())
            {
                emit(BackendAssistantDelta{.text = result->output_text});
            }
            emit(BackendLineComplete{});
            return;
        }

        auto command_result = execute_slash_command(runtime_.commands(), prompt);
        if (!command_result)
        {
            emit(BackendError{.message = command_result.status().message()});
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

        if (command_result->submit_model)
        {
            auto profile = runtime_.find_model_profile(*command_result->submit_model);
            if (!profile)
            {
                emit(BackendError{.message = "unknown model profile: " + *command_result->submit_model});
                emit(BackendLineComplete{});
                return;
            }

            auto switched = runtime_.switch_model_profile(*profile);
            if (!switched)
            {
                emit(BackendError{.message = switched.status().message()});
                emit(BackendLineComplete{});
                return;
            }
        }

        prompt = *command_result->submit_prompt;
    }

    CancellationToken cancellation;
    {
        std::lock_guard lock{state_mutex_};
        if (active_cancellation_)
        {
            cancellation = active_cancellation_->token();
        }
    }

    auto result = runtime_.run_prompt(
        prompt,
        runtime::RunPromptOptions{
            .max_turns = max_turns_,
            .permission_prompt = [this](const PermissionPrompt& prompt) {
                return request_permission(prompt);
            },
            .user_question = [this](const UserQuestionPrompt& prompt) {
                return request_user_question(prompt);
            },
            .cancellation = cancellation,
        },
        [this](const EngineEvent& event) {
            emit_engine_event(event);
        });
    if(!result.ok())
    {
        emit(BackendError{.message = result.status().message()});
        emit(BackendLineComplete{});
        return;
    }

    if (result->usage.normalized_total() > 0)
    {
        emit(BackendUsage{
            .input_tokens = result->usage.input_tokens,
            .output_tokens = result->usage.output_tokens,
            .total_tokens = result->usage.normalized_total(),
        });
    }
    emit(BackendLineComplete{});
}

auto BackendHost::handle_select_command(const FrontendRequest& request) -> void
{
    emit(make_select_request(runtime_.commands(), request.query.value_or(std::string{})));
}

auto BackendHost::handle_apply_select_command(const FrontendRequest& request) -> void
{
    if (!request.command || request.command->empty())
    {
        emit(BackendError{.message = "apply_select_command requires command"});
        return;
    }

    const auto line = format_command_line(*request.command, request.args.value_or(std::string{}));
    auto lookup = runtime_.commands().lookup(line);
    if (lookup.command == nullptr)
    {
        emit(BackendError{.message = "unknown command: " + line.substr(0, line.find_first_of(" \t\r\n"))});
        return;
    }

    handle_submit_line(line);
}

auto BackendHost::handle_select_model() -> void
{
    BackendModelSelectRequest request;
    const auto current = runtime_.current_model_profile();
    for (const auto& profile : runtime_.model_profiles())
    {
        request.profiles.push_back(
            BackendModelEntry{
                .id = profile.id,
                .label = profile.label,
                .description = profile.description,
                .is_current = profile.id == current.id,
            });
    }
    emit(request);
}

auto BackendHost::handle_apply_model(const FrontendRequest& request) -> void
{
    if (!request.profile_id || request.profile_id->empty())
    {
        emit(BackendError{.message = "apply_model requires profile_id"});
        return;
    }

    auto profile = runtime_.find_model_profile(*request.profile_id);
    if (!profile)
    {
        emit(BackendError{.message = "unknown model profile: " + *request.profile_id});
        return;
    }

    auto switched = runtime_.switch_model_profile(*profile);
    if (!switched)
    {
        emit(BackendError{.message = switched.status().message()});
        return;
    }

    emit(BackendAssistantDelta{.text = "Switched model to " + switched->label});
    emit(BackendLineComplete{});
}

auto BackendHost::handle_permission_response(const FrontendRequest& request) -> void
{
    std::unique_lock lock{state_mutex_};
    if (active_run_ && !pending_permission_)
    {
        permission_cv_.wait(lock, [this] {
            return pending_permission_.ok() || !active_run_;
        });
    }

    if (!pending_permission_)
    {
        lock.unlock();
        emit(BackendError{.message = "permission_response has no pending permission request"});
        return;
    }

    if (!request.request_id || *request.request_id != pending_permission_->id)
    {
        lock.unlock();
        emit(BackendError{.message = "permission response request_id mismatch"});
        return;
    }

    if (!request.allowed)
    {
        lock.unlock();
        emit(BackendError{.message = "permission_response requires allowed"});
        return;
    }

    pending_permission_response_ = PermissionResponse{
        .allowed = *request.allowed,
        .reason = *request.allowed ? std::string{} : std::string{"user denied permission"},
        .remember_session = request.remember_session.value_or(false),
    };
    permission_cv_.notify_all();
}

auto BackendHost::handle_user_question_response(const FrontendRequest& request) -> void
{
    std::unique_lock lock{state_mutex_};
    if (active_run_ && !pending_user_question_)
    {
        user_question_cv_.wait(lock, [this] {
            return pending_user_question_.ok() || !active_run_;
        });
    }

    if (!pending_user_question_)
    {
        lock.unlock();
        emit(BackendError{.message = "user_question_response has no pending user question"});
        return;
    }

    if (!request.request_id || *request.request_id != pending_user_question_->id)
    {
        lock.unlock();
        emit(BackendError{.message = "user question response request_id mismatch"});
        return;
    }

    if (!request.answer)
    {
        lock.unlock();
        emit(BackendError{.message = "user_question_response requires answer"});
        return;
    }

    pending_user_question_response_ = UserQuestionResponse{.answer = *request.answer};
    user_question_cv_.notify_all();
}

auto BackendHost::handle_interrupt() -> void
{
    std::lock_guard lock{state_mutex_};
    if (!active_run_)
    {
        emit(BackendError{.message = "interrupt has no active run"});
        return;
    }

    if (active_cancellation_)
    {
        active_cancellation_->cancel();
    }

    if (pending_permission_)
    {
        pending_permission_response_ = PermissionResponse{
            .allowed = false,
            .reason = "interrupted",
        };
        permission_cv_.notify_all();
    }

    if (pending_user_question_)
    {
        pending_user_question_response_.reset();
        user_question_cv_.notify_all();
    }
}

auto BackendHost::emit_engine_event(const EngineEvent& event) -> void
{
    emit(engine_event_to_backend_event(event));
}

auto BackendHost::request_permission(const PermissionPrompt& prompt) -> absl::StatusOr<PermissionResponse>
{
    {
        std::lock_guard lock{state_mutex_};
        pending_permission_ = prompt;
        pending_permission_response_.reset();
    }
    permission_cv_.notify_all();

    emit(BackendPermissionModal{
        .id = prompt.id,
        .tool_use_id = prompt.tool_use_id,
        .tool_name = prompt.tool_name,
        .reason = prompt.reason,
        .path = prompt.path ? std::make_optional(prompt.path->string()) : std::nullopt,
        .command = prompt.command,
    });

    std::unique_lock lock{state_mutex_};
    permission_cv_.wait(lock, [this] {
        return pending_permission_response_.ok() ||
               (active_cancellation_ != nullptr && active_cancellation_->is_cancelled());
    });

    if (!pending_permission_response_)
    {
        pending_permission_.reset();
        return PermissionResponse{.allowed = false, .reason = "interrupted"};
    }

    auto response = *pending_permission_response_;
    pending_permission_response_.reset();
    pending_permission_.reset();
    return response;
}

auto BackendHost::request_user_question(const UserQuestionPrompt& prompt) -> absl::StatusOr<UserQuestionResponse>
{
    {
        std::lock_guard lock{state_mutex_};
        pending_user_question_ = prompt;
        pending_user_question_response_.reset();
    }
    user_question_cv_.notify_all();

    emit(BackendUserQuestionModal{
        .id = prompt.id,
        .tool_use_id = prompt.tool_use_id,
        .question = prompt.question,
        .reason = prompt.reason,
    });

    std::unique_lock lock{state_mutex_};
    user_question_cv_.wait(lock, [this] {
        return pending_user_question_response_.ok() ||
               (active_cancellation_ != nullptr && active_cancellation_->is_cancelled());
    });

    if (!pending_user_question_response_)
    {
        pending_user_question_.reset();
        return absl::StatusOr<UserQuestionResponse>(absl::CancelledError("interrupted"));
    }

    auto response = *pending_user_question_response_;
    pending_user_question_response_.reset();
    pending_user_question_.reset();
    return response;
}

auto BackendHost::has_active_run() const -> bool
{
    std::lock_guard lock{state_mutex_};
    return active_run_;
}

auto BackendHost::reap_finished_run() -> void
{
    if (active_worker_.joinable())
    {
        bool should_join = false;
        {
            std::lock_guard lock{state_mutex_};
            should_join = active_run_finished_;
        }
        if (should_join)
        {
            active_worker_.join();
            std::lock_guard lock{state_mutex_};
            active_run_finished_ = false;
            active_cancellation_.reset();
        }
    }
}

auto BackendHost::wait_for_active_run() -> void
{
    if (active_worker_.joinable())
    {
        active_worker_.join();
    }

    std::lock_guard lock{state_mutex_};
    active_run_ = false;
    active_run_finished_ = false;
    active_cancellation_.reset();
    pending_permission_.reset();
    pending_permission_response_.reset();
    pending_user_question_.reset();
    pending_user_question_response_.reset();
}

} // namespace codeharness::ui_backend

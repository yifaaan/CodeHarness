#include "codeharness/tui/tui_app.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/core/strings.h"
#include "codeharness/tui/tui_composer.h"
#include "codeharness/tui/tui_event.h"
#include "codeharness/tui/tui_render.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace codeharness::tui
{

namespace
{

auto command_entries_from_registry(const CommandRegistry& registry) -> std::vector<CommandPaletteEntry>
{
    std::vector<CommandPaletteEntry> entries;
    for (const auto& command : registry.list())
    {
        entries.push_back(
            CommandPaletteEntry{
                .name = command.name,
                .description = command.description,
                .aliases = command.aliases,
            });
    }
    return entries;
}

auto is_permission_approve_key(const ftxui::Event& event) -> bool
{
    return event == ftxui::Event::Character("y") || event == ftxui::Event::Character("Y");
}

auto is_permission_approve_session_key(const ftxui::Event& event) -> bool
{
    return event == ftxui::Event::Character("a") || event == ftxui::Event::Character("A");
}

auto is_permission_deny_key(const ftxui::Event& event) -> bool
{
    return event == ftxui::Event::Character("d") || event == ftxui::Event::Character("D")
        || event == ftxui::Event::Character("n") || event == ftxui::Event::Character("N")
        || event == ftxui::Event::Escape;
}

auto is_submit_key(const ftxui::Event& event) -> bool
{
    return event == ftxui::Event::Return || event == ftxui::Event::CtrlM;
}

auto resolve_submit_prompt(runtime::RuntimeBundle& runtime,
                           TuiAppModel& model,
                           TuiDisplayConfig& display_config,
                           std::string prompt) -> std::optional<std::string>
{
    if (prompt.empty() || prompt.front() != '/')
    {
        return prompt;
    }

    const auto trimmed = std::string{trim(prompt)};
    if (trimmed == "/plan" || trimmed == "/plan on" || trimmed == "/plan enter")
    {
        runtime.set_permission_mode(PermissionMode::Plan);
        model.set_permission_mode(PermissionMode::Plan);
        model.append_system_message("Entered plan mode. Read-only tools only.");
        return std::nullopt;
    }
    if (trimmed == "/act" || trimmed == "/plan off" || trimmed == "/plan exit")
    {
        runtime.set_permission_mode(PermissionMode::Default);
        model.set_permission_mode(PermissionMode::Default);
        model.append_system_message("Default mode. Mutating tools allowed with confirmation.");
        return std::nullopt;
    }
    if (trimmed == "/fullauto" || trimmed == "/full_auto" || trimmed == "/permissions full_auto")
    {
        runtime.set_permission_mode(PermissionMode::FullAuto);
        model.set_permission_mode(PermissionMode::FullAuto);
        model.append_system_message("Full-auto mode. Mutating tools are allowed unless blocked by safety rules.");
        return std::nullopt;
    }
    if (trimmed == "/default" || trimmed == "/permissions default")
    {
        runtime.set_permission_mode(PermissionMode::Default);
        model.set_permission_mode(PermissionMode::Default);
        model.append_system_message("Default mode. Mutating tools allowed with confirmation.");
        return std::nullopt;
    }
    if (trimmed == "/mode" || trimmed == "/permissions")
    {
        model.append_system_message("Current permission mode: " + std::string{permission_mode_label(runtime.permission_mode())});
        return std::nullopt;
    }

    auto command_result = execute_slash_command(runtime.commands(), prompt);
    if (!command_result)
    {
        model.append_system_message(command_result.error().message);
        return std::nullopt;
    }

    if (command_result->message)
    {
        model.append_system_message(*command_result->message);
    }
    model.set_active_session(runtime.active_session_summary());
    if (command_result->submit_model)
    {
        auto profile = runtime.find_model_profile(*command_result->submit_model);
        if (!profile)
        {
            model.append_system_message("unknown model profile: " + *command_result->submit_model);
            return std::nullopt;
        }

        auto switched = runtime.switch_model_profile(*profile);
        if (!switched)
        {
            model.append_system_message(switched.error().message);
            return std::nullopt;
        }
        display_config.model = switched->provider_config.model;
        display_config.provider_type = switched->description;
        model.append_system_message("Switched model to " + switched->label);
    }
    if (command_result->submit_prompt)
    {
        return *command_result->submit_prompt;
    }
    return std::nullopt;
}

} // namespace

auto TuiAppModel::state() const noexcept -> const TuiState&
{
    return state_;
}

auto TuiAppModel::sync_transcript_view() -> void
{
    state_.transcript = chat_.items();
}

auto TuiAppModel::sync_bottom_pane_view() -> void
{
    const auto& bottom = bottom_pane_.state();
    state_.composer = bottom.composer;
    state_.pending_permission = bottom.pending_permission;
    state_.command_palette = bottom.command_palette;
    state_.select_modal = bottom.select_modal;
    state_.question_modal = bottom.question_modal;
    state_.paste_burst_active = bottom.paste_burst_active;
}

auto TuiAppModel::render_text(int width) const -> std::string
{
    std::ostringstream output;
    if (state_.transcript.empty())
    {
        for (const auto& line : render::render_welcome_lines({}))
        {
            output << line << '\n';
        }
    }
    else
    {
        for (const auto& line : render::render_transcript_lines(state_.transcript, width))
        {
            output << line << '\n';
        }
    }

    if (state_.pending_permission)
    {
        for (const auto& line : render::render_permission_lines(*state_.pending_permission, width))
        {
            output << line << '\n';
        }
    }
    if (state_.command_palette)
    {
        for (const auto& line : render::render_command_palette_lines(*state_.command_palette, width))
        {
            output << line << '\n';
        }
    }
    if (state_.select_modal)
    {
        for (const auto& line : render::render_select_modal_lines(*state_.select_modal, width))
        {
            output << line << '\n';
        }
    }

    output << render::render_status_footer_line({}, state_) << '\n';
    output << (state_.busy ? "Working" : "Ready") << '\n';
    output << "> " << state_.composer;
    return output.str();
}

auto TuiAppModel::set_composer(std::string value) -> void
{
    bottom_pane_.set_composer(std::move(value));
    sync_bottom_pane_view();
}

auto TuiAppModel::open_command_palette(std::vector<CommandPaletteEntry> commands) -> void
{
    bottom_pane_.open_command_palette(std::move(commands), state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::close_command_palette() -> void
{
    bottom_pane_.close_command_palette();
    sync_bottom_pane_view();
}

auto TuiAppModel::update_command_palette_from_composer() -> void
{
    bottom_pane_.update_command_palette_from_composer();
    sync_bottom_pane_view();
}

auto TuiAppModel::command_palette_input(char character) -> void
{
    bottom_pane_.command_palette_input(character, state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::command_palette_backspace() -> void
{
    bottom_pane_.command_palette_backspace(state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::command_palette_up() -> void
{
    bottom_pane_.command_palette_up();
    sync_bottom_pane_view();
}

auto TuiAppModel::command_palette_down() -> void
{
    bottom_pane_.command_palette_down();
    sync_bottom_pane_view();
}

auto TuiAppModel::selected_command_text() const -> std::optional<std::string>
{
    return bottom_pane_.selected_command_text();
}

auto TuiAppModel::handle_submit() -> TuiAction
{
    if (state_.busy || !bottom_pane_.can_submit_prompt())
    {
        return TuiAction::None;
    }
    return TuiAction::SubmitPrompt;
}

auto TuiAppModel::handle_quit() -> TuiAction
{
    if (state_.busy || !bottom_pane_.can_quit())
    {
        return TuiAction::None;
    }
    state_.should_quit = true;
    return TuiAction::Quit;
}

auto TuiAppModel::handle_command_select() -> TuiAction
{
    const auto selected = bottom_pane_.handle_command_select();
    sync_bottom_pane_view();
    return selected ? TuiAction::InsertCommand : TuiAction::None;
}

auto TuiAppModel::handle_command_cancel() -> TuiAction
{
    bottom_pane_.handle_command_cancel();
    sync_bottom_pane_view();
    return TuiAction::None;
}

auto TuiAppModel::handle_interrupt() -> TuiAction
{
    if (!state_.busy && !bottom_pane_.has_pending_permission())
    {
        return TuiAction::None;
    }

    if (!state_.interrupt_requested)
    {
        chat_.append_error_once("interrupted");
        sync_transcript_view();
    }
    state_.interrupt_requested = true;
    bottom_pane_.clear_for_interrupt();
    sync_bottom_pane_view();
    return TuiAction::Interrupt;
}

auto TuiAppModel::handle_permission_approve() -> TuiAction
{
    const auto approved = bottom_pane_.handle_permission_approve();
    sync_bottom_pane_view();
    return approved ? TuiAction::ApprovePermission : TuiAction::None;
}

auto TuiAppModel::handle_permission_approve_for_session() -> TuiAction
{
    const auto approved = bottom_pane_.handle_permission_approve_for_session();
    sync_bottom_pane_view();
    return approved ? TuiAction::ApprovePermissionForSession : TuiAction::None;
}

auto TuiAppModel::handle_permission_deny() -> TuiAction
{
    const auto denied = bottom_pane_.handle_permission_deny();
    sync_bottom_pane_view();
    return denied ? TuiAction::DenyPermission : TuiAction::None;
}

auto TuiAppModel::toggle_tool_details(std::size_t transcript_index) -> bool
{
    const auto toggled = chat_.toggle_tool_details(transcript_index);
    sync_transcript_view();
    return toggled;
}

auto TuiAppModel::begin_prompt(std::string prompt) -> void
{
    chat_.begin_prompt(std::move(prompt));
    sync_transcript_view();
    bottom_pane_.clear_prompt_entry();
    sync_bottom_pane_view();
    state_.interrupt_requested = false;
    state_.busy = true;
}

auto TuiAppModel::complete_prompt() -> void
{
    state_.busy = false;
    state_.interrupt_requested = false;
}

auto TuiAppModel::apply_engine_event(const EngineEvent& event) -> void
{
    chat_.apply_engine_event(event);
    sync_transcript_view();
}

auto TuiAppModel::has_streamed_assistant_output() const noexcept -> bool
{
    return chat_.has_streamed_assistant_output();
}

auto TuiAppModel::show_permission(const PermissionPrompt& prompt) -> void
{
    bottom_pane_.show_permission(prompt);
    sync_bottom_pane_view();
}

auto TuiAppModel::clear_permission() -> void
{
    bottom_pane_.clear_permission();
    sync_bottom_pane_view();
}

auto TuiAppModel::append_system_message(std::string text) -> void
{
    chat_.append_system_message(std::move(text));
    sync_transcript_view();
}

// --- Select modal (/model picker) ---

auto TuiAppModel::open_select_modal(std::string title, std::vector<ModelOption> options) -> void
{
    bottom_pane_.open_select_modal(std::move(title), std::move(options), state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::close_select_modal() -> void
{
    bottom_pane_.close_select_modal();
    sync_bottom_pane_view();
}

auto TuiAppModel::select_modal_up() -> void
{
    bottom_pane_.select_modal_up();
    sync_bottom_pane_view();
}

auto TuiAppModel::select_modal_down() -> void
{
    bottom_pane_.select_modal_down();
    sync_bottom_pane_view();
}

auto TuiAppModel::select_modal_input(char character) -> void
{
    bottom_pane_.select_modal_input(character, state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::select_modal_backspace() -> void
{
    bottom_pane_.select_modal_backspace(state_.busy);
    sync_bottom_pane_view();
}

auto TuiAppModel::handle_select_cancel() -> TuiAction
{
    bottom_pane_.handle_select_cancel();
    sync_bottom_pane_view();
    return TuiAction::None;
}

auto TuiAppModel::select_modal_current() const -> std::optional<ModelOption>
{
    return bottom_pane_.select_modal_current();
}

auto TuiAppModel::select_modal_quick_select(int digit) -> std::optional<ModelOption>
{
    return bottom_pane_.select_modal_quick_select(digit);
}

// --- Question modal (AskUser) ---

auto TuiAppModel::show_question(std::string request_id, std::string question, std::string tool_name, std::string reason)
    -> void
{
    bottom_pane_.show_question(std::move(request_id), std::move(question), std::move(tool_name), std::move(reason));
    sync_bottom_pane_view();
}

auto TuiAppModel::close_question() -> void
{
    bottom_pane_.close_question();
    sync_bottom_pane_view();
}

auto TuiAppModel::question_modal_input(char character) -> void
{
    bottom_pane_.question_modal_input(character);
    sync_bottom_pane_view();
}

auto TuiAppModel::question_modal_backspace() -> void
{
    bottom_pane_.question_modal_backspace();
    sync_bottom_pane_view();
}

auto TuiAppModel::question_modal_newline() -> void
{
    bottom_pane_.question_modal_newline();
    sync_bottom_pane_view();
}

auto TuiAppModel::question_modal_submit() -> std::string
{
    return bottom_pane_.question_modal_submit();
}

auto TuiAppModel::set_active_session(std::optional<SessionCommandSummary> summary) -> void
{
    state_.active_session = std::move(summary);
}

// --- Paste burst detection ---

auto TuiAppModel::detect_paste_burst(const std::string& input) -> void
{
    bottom_pane_.detect_paste_burst(input);
    sync_bottom_pane_view();
}

auto TuiAppModel::apply_paste_to_composer(const std::string& paste_text) -> void
{
    bottom_pane_.apply_paste_to_composer(paste_text, state_.busy);
    sync_bottom_pane_view();
}

auto apply_transcript_follow_wheel(bool current_follow, bool wheel_up, bool wheel_down) -> bool
{
    if (wheel_up)
    {
        return false;
    }
    if (wheel_down)
    {
        return true;
    }
    return current_follow;
}

auto run_tui(runtime::RuntimeBundle& runtime,
             int max_turns,
             TuiDisplayConfig display_config,
             ModelListProvider model_list_provider,
             ModelSelectCallback model_select_callback) -> Result<int>
{
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    TuiAppModel model;
    auto command_entries = command_entries_from_registry(runtime.commands());

    std::mutex mutex;
    std::condition_variable permission_cv;
    std::condition_variable user_question_cv;
    std::optional<PermissionResponse> permission_response;
    std::optional<UserQuestionResponse> user_question_response;
    std::unique_ptr<CancellationSource> cancellation_source;
    std::thread worker;
    bool worker_finished = false;
    auto screen_alive = std::make_shared<std::atomic<bool>>(true);
    TuiEventQueue tui_events;
    auto wake_ui = [screen_alive, &screen] {
        if (screen_alive->load(std::memory_order_acquire))
        {
            screen.PostEvent(Event::Custom);
        }
    };
    TuiEventSender event_sender{tui_events, wake_ui};
    bool follow_transcript = true;
    int spinner_frame = 0;
    auto last_spinner_tick = std::chrono::steady_clock::now();
    int last_transcript_count = 0;

    auto drain_tui_events = [&] {
        auto events = tui_events.drain();
        if (events.empty())
        {
            return;
        }

        std::lock_guard lock{mutex};
        for (auto& event : events)
        {
            std::visit(
                Overloaded{
                    [&](TuiEngineEvent& engine_event) {
                        model.apply_engine_event(engine_event.event);
                    },
                    [&](TuiRunCompleted& completed) {
                        if (!completed.success)
                        {
                            model.apply_engine_event(EngineError{.message = completed.error_message});
                        }
                        else if (!completed.output_text.empty() && !model.has_streamed_assistant_output())
                        {
                            model.apply_engine_event(EngineAssistantTextDelta{.text = completed.output_text});
                        }
                        if (completed.success)
                        {
                            display_config.token_usage = TokenUsage{
                                .input_tokens = completed.input_tokens,
                                .output_tokens = completed.output_tokens,
                            };
                        }
                        model.clear_permission();
                        model.close_question();
                        model.complete_prompt();
                        model.set_permission_mode(runtime.permission_mode());
                        model.set_active_session(runtime.active_session_summary());
                        user_question_response.reset();
                        cancellation_source.reset();
                        worker_finished = true;
                    },
                    [&](TuiPermissionRequested& requested) {
                        permission_response.reset();
                        model.show_permission(requested.prompt);
                    },
                    [&](TuiQuestionRequested& requested) {
                        user_question_response.reset();
                        model.show_question(
                            requested.prompt.id,
                            requested.prompt.question,
                            "ask_user",
                            requested.prompt.reason);
                    },
                    [](TuiRefreshRequested&) {},
                },
                event);
        }
    };

    auto reap_worker = [&] {
        bool should_join = false;
        {
            std::lock_guard lock{mutex};
            should_join = worker_finished;
        }
        if (should_join && worker.joinable())
        {
            worker.join();
            std::lock_guard lock{mutex};
            worker_finished = false;
        }
    };

    auto run_prompt = [&](std::string prompt) {
        reap_worker();
        CancellationToken cancellation;
        {
            std::lock_guard lock{mutex};
            model.begin_prompt(prompt);
            follow_transcript = true;
            cancellation_source = std::make_unique<CancellationSource>();
            cancellation = cancellation_source->token();
        }

        worker = std::thread{[&, prompt = std::move(prompt), cancellation] {
            const auto complete_with_error = [&](std::string message) {
                event_sender.send(TuiRunCompleted{.success = false, .error_message = std::move(message)});
            };

            try
            {
                auto result = runtime.run_prompt(
                    prompt,
                    runtime::RunPromptOptions{
                        .max_turns = max_turns,
                        .permission_prompt = [&](const PermissionPrompt& permission_prompt) -> Result<PermissionResponse> {
                            event_sender.send(TuiPermissionRequested{.prompt = permission_prompt});

                            {
                                std::unique_lock lock{mutex};
                                permission_cv.wait(lock, [&] { return permission_response.has_value() || model.state().interrupt_requested; });
                                auto response = permission_response.value_or(PermissionResponse{.allowed = false, .reason = "interrupted"});
                                permission_response.reset();
                                lock.unlock();

                                event_sender.send(TuiRefreshRequested{});
                                return response;
                            }
                        },
                        .user_question = [&](const UserQuestionPrompt& question_prompt) -> Result<UserQuestionResponse> {
                            event_sender.send(TuiQuestionRequested{.prompt = question_prompt});

                            {
                                std::unique_lock lock{mutex};
                                user_question_cv.wait(lock, [&] {
                                    return user_question_response.has_value() || model.state().interrupt_requested;
                                });
                                if (!user_question_response)
                                {
                                    lock.unlock();
                                    event_sender.send(TuiRefreshRequested{});
                                    return fail<UserQuestionResponse>(ErrorKind::Cancelled, "interrupted");
                                }

                                auto response = *user_question_response;
                                user_question_response.reset();
                                lock.unlock();

                                event_sender.send(TuiRefreshRequested{});
                                return response;
                            }
                        },
                        .cancellation = cancellation,
                    },
                    [&](const EngineEvent& event) {
                        event_sender.send(TuiEngineEvent{.event = event});
                    });

                if (!result)
                {
                    event_sender.send(TuiRunCompleted{.success = false, .error_message = result.error().message});
                    return;
                }

                event_sender.send(
                    TuiRunCompleted{
                        .success = true,
                        .output_text = result->output_text,
                        .input_tokens = result->usage.input_tokens,
                        .output_tokens = result->usage.output_tokens,
                    });
            }
            catch (const std::exception& error)
            {
                complete_with_error(std::string{"unexpected runtime error: "} + error.what());
            }
            catch (...)
            {
                complete_with_error("unexpected runtime error");
            }
        }};
    };

    ComposerState composer_state;
    auto composer_component = make_multiline_composer(composer_state);
    auto component = CatchEvent(composer_component, [&](Event event) {
        drain_tui_events();
        reap_worker();

        // --- Paste burst detection ---
        // ftxui delivers pasted text as a single event with multiple characters.
        // Detect multi-char non-control input and inject it directly into the composer.
        if (event.is_character())
        {
            const auto& input = event.input();
            if (input.size() > 1)
            {
                std::lock_guard lock{mutex};
                model.detect_paste_burst(input);
                model.apply_paste_to_composer(input);
                composer_state.set_content(model.state().composer);
                return true;
            }
        }

        {
            bool handled_permission = false;
            bool notify_permission = false;
            {
                std::lock_guard lock{mutex};
                if (model.state().pending_permission)
                {
                    handled_permission = true;
                    if (event == Event::CtrlC)
                    {
                        if (cancellation_source)
                        {
                            cancellation_source->cancel();
                        }
                        model.handle_interrupt();
                        permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
                        notify_permission = true;
                    }
                    else if (is_permission_approve_key(event))
                    {
                        if (model.handle_permission_approve() == TuiAction::ApprovePermission)
                        {
                            permission_response = PermissionResponse{.allowed = true};
                            notify_permission = true;
                        }
                    }
                    else if (is_permission_approve_session_key(event))
                    {
                        if (model.handle_permission_approve_for_session() == TuiAction::ApprovePermissionForSession)
                        {
                            permission_response = PermissionResponse{.allowed = true, .remember_session = true};
                            notify_permission = true;
                        }
                    }
                    else if (is_permission_deny_key(event))
                    {
                        if (model.handle_permission_deny() == TuiAction::DenyPermission)
                        {
                            permission_response = PermissionResponse{.allowed = false, .reason = "user denied permission"};
                            notify_permission = true;
                        }
                    }
                }
            }
            if (handled_permission)
            {
                if (notify_permission)
                {
                    permission_cv.notify_one();
                }
                return true;
            }
        }

        // --- Select modal (/model) ---
        {
            std::lock_guard lock{mutex};
            if (model.state().select_modal)
            {
                if (event == Event::ArrowUp)
                {
                    model.select_modal_up();
                    return true;
                }
                if (event == Event::ArrowDown)
                {
                    model.select_modal_down();
                    return true;
                }
                if (is_submit_key(event))
                {
                    auto selected = model.select_modal_current();
                    model.close_select_modal();
                    if (selected && model_select_callback)
                    {
                        auto switched = model_select_callback(*selected);
                        if (switched)
                        {
                            display_config.model = switched->value;
                            display_config.provider_type = switched->description;
                            model.append_system_message("Switched model to " + switched->label);
                        }
                        else
                        {
                            model.append_system_message(switched.error().message);
                        }
                    }
                    return true;
                }
                if (event == Event::Escape)
                {
                    model.handle_select_cancel();
                    return true;
                }
                if (event == Event::Backspace)
                {
                    model.select_modal_backspace();
                    return true;
                }
                // Number keys 1-9 for quick selection
                if (event.is_character())
                {
                    const auto& input = event.input();
                    if (input.size() == 1)
                    {
                        const auto digit = input.front();
                        if (digit >= '1' && digit <= '9')
                        {
                            auto selected = model.select_modal_quick_select(digit - '0');
                            if (selected)
                            {
                                model.close_select_modal();
                                if (model_select_callback)
                                {
                                    auto switched = model_select_callback(*selected);
                                    if (switched)
                                    {
                                        display_config.model = switched->value;
                                        display_config.provider_type = switched->description;
                                        model.append_system_message("Switched model to " + switched->label);
                                    }
                                    else
                                    {
                                        model.append_system_message(switched.error().message);
                                    }
                                }
                            }
                            return true;
                        }
                        if (std::isprint(static_cast<unsigned char>(digit)) != 0)
                        {
                            model.select_modal_input(digit);
                            return true;
                        }
                    }
                }
                return true;
            }
        }

        // --- Question modal (AskUser) ---
        {
            std::lock_guard lock{mutex};
            if (model.state().question_modal)
            {
                if (is_submit_key(event))
                {
                    // Shift+Enter = newline in question modal
                    if (is_composer_newline_event(event))
                    {
                        model.question_modal_newline();
                        return true;
                    }
                    user_question_response = UserQuestionResponse{.answer = model.question_modal_submit()};
                    model.close_question();
                    user_question_cv.notify_one();
                    return true;
                }
                if (event == Event::Backspace)
                {
                    model.question_modal_backspace();
                    return true;
                }
                if (is_composer_newline_event(event))
                {
                    model.question_modal_newline();
                    return true;
                }
                if (event.is_character())
                {
                    const auto& input = event.input();
                    if (input.size() == 1 && std::isprint(static_cast<unsigned char>(input.front())) != 0)
                    {
                        model.question_modal_input(input.front());
                    }
                    return true;
                }
                return true;
            }
        }

        if (event == Event::CtrlC || event == Event::CtrlD)
        {
            std::lock_guard lock{mutex};
            if (model.state().busy)
            {
                if (cancellation_source)
                {
                    cancellation_source->cancel();
                }
                model.handle_interrupt();
                permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
                permission_cv.notify_all();
                user_question_response.reset();
                user_question_cv.notify_all();
                return true;
            }
            if (model.handle_quit() == TuiAction::Quit)
            {
                screen.ExitLoopClosure()();
            }
            return true;
        }

        if (event == Event::Escape && model.state().busy)
        {
            std::lock_guard lock{mutex};
            if (cancellation_source)
            {
                cancellation_source->cancel();
            }
            model.handle_interrupt();
            permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
            permission_cv.notify_all();
            user_question_response.reset();
            user_question_cv.notify_all();
            return true;
        }

        {
            std::lock_guard lock{mutex};
            if (model.state().busy)
            {
                return true;
            }
        }

        {
            std::lock_guard lock{mutex};
            if (model.state().command_palette)
            {
                if (event == Event::ArrowUp)
                {
                    model.command_palette_up();
                    return true;
                }
                if (event == Event::ArrowDown)
                {
                    model.command_palette_down();
                    return true;
                }
                if (event == Event::Escape)
                {
                    model.handle_command_cancel();
                    composer_state.set_content(model.state().composer);
                    return true;
                }
                if (event == Event::Backspace)
                {
                    model.command_palette_backspace();
                    composer_state.set_content(model.state().composer);
                    return true;
                }
                if (is_submit_key(event))
                {
                    model.handle_command_select();
                    composer_state.set_content(model.state().composer);
                    return true;
                }
                if (event.is_character())
                {
                    const auto characters = event.input();
                    if (characters.size() == 1 && std::isprint(static_cast<unsigned char>(characters.front())) != 0)
                    {
                        model.command_palette_input(characters.front());
                        composer_state.set_content(model.state().composer);
                        return true;
                    }
                }
            }
        }

        if (is_composer_newline_event(event))
        {
            composer_state.insert_newline();
            return true;
        }

        if (is_submit_key(event))
        {
            std::string prompt;
            {
                std::lock_guard lock{mutex};
                model.set_composer(composer_state.content());

                // Intercept /model to open the select modal
                auto trimmed = std::string{trim(model.state().composer)};
                if (trimmed == "/model" && model_list_provider)
                {
                    auto options = model_list_provider();
                    if (!options.empty())
                    {
                        model.open_select_modal("Select model", std::move(options));
                    }
                    else
                    {
                        model.append_system_message("No models available.");
                    }
                    composer_state.clear();
                    model.set_composer("");
                    return true;
                }

                if (model.handle_submit() != TuiAction::SubmitPrompt)
                {
                    return true;
                }
                prompt = composer_state.content();
            }

            composer_state.push_history(prompt);
            composer_state.clear();
            {
                std::lock_guard lock{mutex};
                model.set_composer("");
            }

            if (auto engine_prompt = resolve_submit_prompt(runtime, model, display_config, std::move(prompt)))
            {
                run_prompt(std::move(*engine_prompt));
            }
            return true;
        }

        if (event == Event::Character("/") && composer_state.content().empty())
        {
            std::lock_guard lock{mutex};
            composer_state.set_content("/");
            model.set_composer("/");
            model.open_command_palette(command_entries);
            return true;
        }

        if (event.is_mouse())
        {
            const auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp)
            {
                follow_transcript = apply_transcript_follow_wheel(follow_transcript, true, false);
                return true;
            }
            if (mouse.button == Mouse::WheelDown)
            {
                follow_transcript = apply_transcript_follow_wheel(follow_transcript, false, true);
                return true;
            }
        }

        return false;
    });

    auto renderer = Renderer(component, [&] {
        drain_tui_events();

        std::lock_guard lock{mutex};

        if (model.state().busy)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_spinner_tick >= std::chrono::milliseconds{120})
            {
                spinner_frame = (spinner_frame + 1) % 10;
                last_spinner_tick = now;
            }
        }
        else
        {
            spinner_frame = 0;
            last_spinner_tick = std::chrono::steady_clock::now();
        }

        const auto terminal_width = std::max(screen.dimx(), 40);
        Elements rows;

        if (model.state().transcript.empty())
        {
            rows.push_back(render::welcome_banner_element(display_config));
        }
        else
        {
            Elements transcript_rows;
            for (const auto& item : model.state().transcript)
            {
                transcript_rows.push_back(render::transcript_item_element(item, terminal_width));
            }

            auto transcript_box = vbox(std::move(transcript_rows)) | flex;
            if (follow_transcript)
            {
                transcript_box = transcript_box | focusPositionRelative(0.f, 1.f);
            }
            rows.push_back(transcript_box | yframe | vscroll_indicator);
        }

        if (model.state().pending_permission)
        {
            rows.push_back(text(" "));
            rows.push_back(render::permission_modal_element(*model.state().pending_permission, terminal_width));
        }
        else if (model.state().select_modal)
        {
            rows.push_back(text(" "));
            rows.push_back(render::select_modal_element(*model.state().select_modal, terminal_width));
        }
        else if (model.state().question_modal)
        {
            rows.push_back(text(" "));
            rows.push_back(render::question_modal_element(*model.state().question_modal, terminal_width));
        }
        else if (model.state().command_palette)
        {
            rows.push_back(text(" "));
            rows.push_back(render::command_palette_element(*model.state().command_palette, terminal_width));
        }

        rows.push_back(text(" "));
        rows.push_back(text(render::horizontal_rule(terminal_width)) | dim);
        rows.push_back(render::status_footer_element(display_config, model.state()));

        if (!model.state().pending_permission && !model.state().select_modal && !model.state().question_modal)
        {
            const auto status_text = model.state().busy ? render::busy_spinner_frame(spinner_frame) + " Working"
                                                        : "Ready";
            rows.push_back(hbox({
                text(status_text) | (model.state().busy ? color(TuiTheme::warning()) : color(TuiTheme::success())),
                text(" "),
                composer_component->Render() | borderRounded | size(HEIGHT, GREATER_THAN, 3) | flex,
            }));
            rows.push_back(text(render::render_composer_hint(model.state().busy, composer_state.history_index())) | dim);
        }

        if (static_cast<int>(model.state().transcript.size()) != last_transcript_count)
        {
            follow_transcript = true;
            last_transcript_count = static_cast<int>(model.state().transcript.size());
        }

        return vbox(std::move(rows)) | flex;
    });

    screen.Loop(renderer);

    screen_alive->store(false, std::memory_order_release);
    drain_tui_events();

    {
        std::lock_guard lock{mutex};
        if (cancellation_source)
        {
            cancellation_source->cancel();
        }
        permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
        model.handle_interrupt();
    }
    permission_cv.notify_all();
    user_question_cv.notify_all();

    if (worker.joinable())
    {
        worker.join();
    }
    drain_tui_events();

    std::cout << '\n';
    return 0;
}

} // namespace codeharness::tui

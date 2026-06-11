#include "codeharness/tui/tui_app.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/strings.h"
#include "codeharness/tui/terminal.h"
#include "codeharness/tui/tui_composer.h"
#include "codeharness/tui/tui_event.h"
#include "codeharness/tui/tui_render.h"
#include "codeharness/tui/tui_theme.h"
#include "codeharness/tui/style.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <cassert>
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
#include <variant>

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

auto is_printable_ascii(char character) -> bool
{
    return std::isprint(static_cast<unsigned char>(character)) != 0;
}

auto is_permission_approve_input(const TuiInput& input) -> bool
{
    return input.kind == TuiInputKind::character && (input.character == 'y' || input.character == 'Y');
}

auto is_permission_approve_session_input(const TuiInput& input) -> bool
{
    return input.kind == TuiInputKind::character && (input.character == 'a' || input.character == 'A');
}

auto is_permission_deny_input(const TuiInput& input) -> bool
{
    return input.kind == TuiInputKind::escape
        || (input.kind == TuiInputKind::character
            && (input.character == 'd' || input.character == 'D' || input.character == 'n' || input.character == 'N'));
}

auto is_submit_key(const ftxui::Event& event) -> bool
{
    return event == ftxui::Event::Return || event == ftxui::Event::CtrlM;
}

auto tui_input_from_event(const ftxui::Event& event) -> TuiInput
{
    if (event == ftxui::Event::ArrowUp)
    {
        return TuiInput{.kind = TuiInputKind::arrow_up};
    }
    if (event == ftxui::Event::ArrowDown)
    {
        return TuiInput{.kind = TuiInputKind::arrow_down};
    }
    if (event == ftxui::Event::Backspace)
    {
        return TuiInput{.kind = TuiInputKind::backspace};
    }
    if (event == ftxui::Event::Escape)
    {
        return TuiInput{.kind = TuiInputKind::escape};
    }
    if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD)
    {
        return TuiInput{.kind = TuiInputKind::interrupt};
    }
    if (is_composer_newline_event(event))
    {
        return TuiInput{.kind = TuiInputKind::newline};
    }
    if (is_submit_key(event))
    {
        return TuiInput{.kind = TuiInputKind::submit};
    }
    if (event.is_character())
    {
        const auto input = event.input();
        if (input.size() == 1 && is_printable_ascii(input.front()))
        {
            return TuiInput{.kind = TuiInputKind::character, .character = input.front()};
        }
    }
    return TuiInput{.kind = TuiInputKind::unknown};
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
    const auto previous_revision = state_.transcript_revision;
    state_.transcript = chat_.items();
    state_.transcript_revision = chat_.revision();
    if (state_.transcript_revision != previous_revision)
    {
        state_.follow_transcript = true;
    }
}

auto TuiAppModel::sync_bottom_pane_view() -> void
{
    const auto& bottom = bottom_pane_.state();
    state_.composer = bottom.composer;
    state_.bottom_pane_focus = bottom.focus;
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

    if (state_.bottom_pane_focus == BottomPaneFocus::permission_prompt && state_.pending_permission)
    {
        for (const auto& line : render::render_permission_lines(*state_.pending_permission, width))
        {
            output << line << '\n';
        }
    }
    else if (state_.bottom_pane_focus == BottomPaneFocus::command_palette && state_.command_palette)
    {
        for (const auto& line : render::render_command_palette_lines(*state_.command_palette, width))
        {
            output << line << '\n';
        }
    }
    else if (state_.bottom_pane_focus == BottomPaneFocus::select_modal && state_.select_modal)
    {
        for (const auto& line : render::render_select_modal_lines(*state_.select_modal, width))
        {
            output << line << '\n';
        }
    }
    else if (state_.bottom_pane_focus == BottomPaneFocus::question_modal && state_.question_modal)
    {
        for (const auto& line : render::render_question_lines(*state_.question_modal, width))
        {
            output << line << '\n';
        }
    }

    output << render::render_status_footer_line({}, state_) << '\n';
    if (bottom_pane_accepts_composer_input(state_.bottom_pane_focus))
    {
        output << (state_.busy ? "Working" : "Ready") << '\n';
        output << "> " << state_.composer;
    }
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

auto TuiAppModel::handle_composer_submit(std::string content) -> TuiComposerSubmitResult
{
    TuiComposerSubmitResult result{.handled = true};
    set_composer(std::move(content));

    const auto trimmed = std::string{trim(state_.composer)};
    if (trimmed == "/model")
    {
        set_composer("");
        result.request_model_selector = true;
        result.composer_changed = true;
        return result;
    }

    if (handle_submit() != TuiAction::SubmitPrompt)
    {
        return result;
    }

    result.action = TuiAction::SubmitPrompt;
    result.prompt = state_.composer;
    set_composer("");
    result.composer_changed = true;
    return result;
}

auto TuiAppModel::handle_composer_slash_start(std::vector<CommandPaletteEntry> commands) -> bool
{
    if (state_.busy || !state_.composer.empty())
    {
        return false;
    }

    set_composer("/");
    open_command_palette(std::move(commands));
    return state_.bottom_pane_focus == BottomPaneFocus::command_palette;
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

auto TuiAppModel::request_interrupt() -> TuiInterruptResult
{
    TuiInterruptResult result;
    if (handle_interrupt() != TuiAction::Interrupt)
    {
        return result;
    }

    result.interrupted = true;
    result.permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
    result.cancel_user_question = true;
    return result;
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
    assert(!state_.transcript.empty() && "begin_prompt must produce at least one transcript item");
    bottom_pane_.clear_prompt_entry();
    sync_bottom_pane_view();
    state_.interrupt_requested = false;
    state_.busy = true;
}

auto TuiAppModel::complete_prompt() -> void
{
    chat_.finish_active_response();
    sync_transcript_view();
    assert(!state_.transcript.empty() && "complete_prompt should preserve transcript items from begin_prompt");
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

auto TuiAppModel::has_active_response() const noexcept -> bool
{
    return chat_.has_active_response();
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

auto TuiAppModel::handle_focused_bottom_pane_input(const TuiInput& input) -> TuiFocusedInputResult
{
    TuiFocusedInputResult result;

    if (state_.bottom_pane_focus == BottomPaneFocus::permission_prompt && state_.pending_permission)
    {
        result.handled = true;
        if (input.kind == TuiInputKind::interrupt)
        {
            result.interrupt = request_interrupt();
            if (result.interrupt.interrupted)
            {
                result.action = TuiAction::Interrupt;
            }
            return result;
        }
        if (is_permission_approve_input(input))
        {
            if (handle_permission_approve() == TuiAction::ApprovePermission)
            {
                result.action = TuiAction::ApprovePermission;
                result.permission_response = PermissionResponse{.allowed = true};
            }
            return result;
        }
        if (is_permission_approve_session_input(input))
        {
            if (handle_permission_approve_for_session() == TuiAction::ApprovePermissionForSession)
            {
                result.action = TuiAction::ApprovePermissionForSession;
                result.permission_response = PermissionResponse{.allowed = true, .remember_session = true};
            }
            return result;
        }
        if (is_permission_deny_input(input))
        {
            if (handle_permission_deny() == TuiAction::DenyPermission)
            {
                result.action = TuiAction::DenyPermission;
                result.permission_response = PermissionResponse{.allowed = false, .reason = "user denied permission"};
            }
            return result;
        }
        return result;
    }

    if (state_.bottom_pane_focus == BottomPaneFocus::select_modal && state_.select_modal)
    {
        result.handled = true;
        if (input.kind == TuiInputKind::arrow_up)
        {
            select_modal_up();
            return result;
        }
        if (input.kind == TuiInputKind::arrow_down)
        {
            select_modal_down();
            return result;
        }
        if (input.kind == TuiInputKind::submit)
        {
            result.selected_model = select_modal_current();
            close_select_modal();
            if (result.selected_model)
            {
                result.action = TuiAction::SelectModel;
            }
            return result;
        }
        if (input.kind == TuiInputKind::escape)
        {
            handle_select_cancel();
            return result;
        }
        if (input.kind == TuiInputKind::backspace)
        {
            select_modal_backspace();
            return result;
        }
        if (input.kind == TuiInputKind::character)
        {
            const auto digit = input.character;
            if (digit >= '1' && digit <= '9')
            {
                result.selected_model = select_modal_quick_select(digit - '0');
                if (result.selected_model)
                {
                    close_select_modal();
                    result.action = TuiAction::SelectModel;
                }
                return result;
            }
            if (is_printable_ascii(digit))
            {
                select_modal_input(digit);
                return result;
            }
        }
        return result;
    }

    if (state_.bottom_pane_focus == BottomPaneFocus::question_modal && state_.question_modal)
    {
        result.handled = true;
        if (input.kind == TuiInputKind::interrupt || input.kind == TuiInputKind::escape)
        {
            result.interrupt = request_interrupt();
            if (result.interrupt.interrupted)
            {
                result.action = TuiAction::Interrupt;
            }
            return result;
        }
        if (input.kind == TuiInputKind::submit)
        {
            result.action = TuiAction::SubmitQuestion;
            result.user_question_response = UserQuestionResponse{.answer = question_modal_submit()};
            close_question();
            return result;
        }
        if (input.kind == TuiInputKind::backspace)
        {
            question_modal_backspace();
            return result;
        }
        if (input.kind == TuiInputKind::newline)
        {
            question_modal_newline();
            return result;
        }
        if (input.kind == TuiInputKind::character && is_printable_ascii(input.character))
        {
            question_modal_input(input.character);
            return result;
        }
        return result;
    }

    if (state_.bottom_pane_focus == BottomPaneFocus::command_palette && state_.command_palette)
    {
        if (input.kind == TuiInputKind::arrow_up)
        {
            command_palette_up();
            result.handled = true;
            return result;
        }
        if (input.kind == TuiInputKind::arrow_down)
        {
            command_palette_down();
            result.handled = true;
            return result;
        }
        if (input.kind == TuiInputKind::escape)
        {
            handle_command_cancel();
            result.handled = true;
            result.composer_changed = true;
            return result;
        }
        if (input.kind == TuiInputKind::backspace)
        {
            command_palette_backspace();
            result.handled = true;
            result.composer_changed = true;
            return result;
        }
        if (input.kind == TuiInputKind::submit)
        {
            handle_command_select();
            result.handled = true;
            result.composer_changed = true;
            return result;
        }
        if (input.kind == TuiInputKind::character && is_printable_ascii(input.character))
        {
            command_palette_input(input.character);
            result.handled = true;
            result.composer_changed = true;
            return result;
        }
    }

    return result;
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

auto TuiAppModel::handle_composer_paste(std::string paste_text) -> TuiComposerPasteResult
{
    const auto before = state_.composer;
    detect_paste_burst(paste_text);
    if (!state_.paste_burst_active)
    {
        return {};
    }

    apply_paste_to_composer(paste_text);
    return TuiComposerPasteResult{
        .handled = true,
        .composer_changed = state_.composer != before,
    };
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

auto TuiAppModel::handle_transcript_wheel(bool wheel_up, bool wheel_down) -> bool
{
    if (!wheel_up && !wheel_down)
    {
        return false;
    }

    state_.follow_transcript = apply_transcript_follow_wheel(state_.follow_transcript, wheel_up, wheel_down);
    return true;
}

auto run_tui(runtime::RuntimeBundle& runtime,
             int max_turns,
             TuiDisplayConfig display_config,
             ModelListProvider model_list_provider,
             ModelSelectCallback model_select_callback) -> Result<int>
{
    using namespace ftxui;

    TuiTerminalSession terminal;
    TuiAppModel model;
    auto command_entries = command_entries_from_registry(runtime.commands());

    std::mutex mutex;
    std::condition_variable permission_cv;
    std::condition_variable user_question_cv;
    std::optional<PermissionResponse> permission_response;
    std::optional<UserQuestionResponse> user_question_response;
    std::shared_ptr<CancellationSource> cancellation_source;
    std::thread worker;
    bool worker_finished = false;
    TuiEventQueue tui_events;
    auto wake_ui = [&terminal] { terminal.post_refresh(); };
    TuiEventSender event_sender{tui_events, wake_ui};
    int spinner_frame = 0;
    auto last_spinner_tick = std::chrono::steady_clock::now();

    auto drain_tui_events = [&] {
        auto events = tui_events.drain();
        if (events.empty())
        {
            return;
        }

        std::lock_guard lock{mutex};
        for (const auto& event : events)
        {
            auto context = TuiAppEventContext{};
            if (std::holds_alternative<TuiRunCompleted>(event))
            {
                context = TuiAppEventContext{
                    .permission_mode = runtime.permission_mode(),
                    .active_session = runtime.active_session_summary(),
                };
            }

            const auto result = model.apply_tui_event(event, context);
            if (result.token_usage)
            {
                display_config.token_usage = *result.token_usage;
            }
            if (result.clear_permission_response)
            {
                permission_response.reset();
            }
            if (result.clear_user_question_response)
            {
                user_question_response.reset();
            }
            if (result.release_cancellation)
            {
                cancellation_source.reset();
            }
            if (result.run_completed)
            {
                worker_finished = true;
            }
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
            cancellation_source = std::make_shared<CancellationSource>();
            cancellation = cancellation_source->token();
        }
        terminal.post_refresh();

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

    auto apply_selected_model = [&](const ModelOption& selected) {
        if (!model_select_callback)
        {
            return;
        }

        auto switched = model_select_callback(selected);
        std::lock_guard lock{mutex};
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
    };

    auto apply_interrupt_result = [&](const TuiInterruptResult& interrupt,
                                      std::shared_ptr<CancellationSource>& source_to_cancel,
                                      bool& notify_permission,
                                      bool& notify_question) {
        if (!interrupt.interrupted)
        {
            return;
        }
        source_to_cancel = cancellation_source;
        if (interrupt.permission_response)
        {
            permission_response = *interrupt.permission_response;
            notify_permission = true;
        }
        if (interrupt.cancel_user_question)
        {
            user_question_response.reset();
            notify_question = true;
        }
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
                const auto paste_result = model.handle_composer_paste(input);
                if (paste_result.composer_changed)
                {
                    composer_state.set_content(model.state().composer);
                }
                return paste_result.handled;
            }
        }

        {
            auto focused_result = TuiFocusedInputResult{};
            std::shared_ptr<CancellationSource> source_to_cancel;
            bool notify_permission = false;
            bool notify_question = false;
            std::optional<ModelOption> selected_model;
            {
                std::lock_guard lock{mutex};
                focused_result = model.handle_focused_bottom_pane_input(tui_input_from_event(event));
                apply_interrupt_result(focused_result.interrupt, source_to_cancel, notify_permission, notify_question);
                if (focused_result.permission_response)
                {
                    permission_response = *focused_result.permission_response;
                    notify_permission = true;
                }
                if (focused_result.user_question_response)
                {
                    user_question_response = *focused_result.user_question_response;
                    notify_question = true;
                }
                if (focused_result.composer_changed)
                {
                    composer_state.set_content(model.state().composer);
                }
                selected_model = focused_result.selected_model;
            }
            if (focused_result.handled)
            {
                if (source_to_cancel)
                {
                    source_to_cancel->cancel();
                }
                if (selected_model)
                {
                    apply_selected_model(*selected_model);
                }
                if (notify_permission)
                {
                    permission_cv.notify_one();
                }
                if (notify_question)
                {
                    user_question_cv.notify_one();
                }
                return true;
            }
        }

        if (event == Event::CtrlC || event == Event::CtrlD)
        {
            bool handled_interrupt = false;
            std::shared_ptr<CancellationSource> source_to_cancel;
            bool notify_permission = false;
            bool notify_question = false;
            {
                std::lock_guard lock{mutex};
                if (model.state().busy)
                {
                    const auto interrupt = model.request_interrupt();
                    handled_interrupt = interrupt.interrupted;
                    apply_interrupt_result(interrupt, source_to_cancel, notify_permission, notify_question);
                }
                else if (model.handle_quit() == TuiAction::Quit)
                {
                    terminal.exit_loop();
                }
            }
            if (handled_interrupt)
            {
                if (source_to_cancel)
                {
                    source_to_cancel->cancel();
                }
                if (notify_permission)
                {
                    permission_cv.notify_all();
                }
                if (notify_question)
                {
                    user_question_cv.notify_all();
                }
            }
            return true;
        }

        if (event == Event::Escape)
        {
            bool handled_interrupt = false;
            bool was_busy = false;
            std::shared_ptr<CancellationSource> source_to_cancel;
            bool notify_permission = false;
            bool notify_question = false;
            {
                std::lock_guard lock{mutex};
                was_busy = model.state().busy;
                if (was_busy)
                {
                    const auto interrupt = model.request_interrupt();
                    handled_interrupt = interrupt.interrupted;
                    apply_interrupt_result(interrupt, source_to_cancel, notify_permission, notify_question);
                }
            }
            if (!was_busy)
            {
                return false;
            }
            if (handled_interrupt)
            {
                if (source_to_cancel)
                {
                    source_to_cancel->cancel();
                }
                if (notify_permission)
                {
                    permission_cv.notify_all();
                }
                if (notify_question)
                {
                    user_question_cv.notify_all();
                }
            }
            return true;
        }

        // Allow Event::Custom through even when busy — it is a repaint request
        // from terminal.post_refresh(), not user input. Without this, the
        // renderer never paints the user message added by begin_prompt() until
        // the worker completes, making it appear as if the message was lost.
        if (event == Event::Custom)
        {
            return false;
        }

        {
            std::lock_guard lock{mutex};
            if (model.state().busy)
            {
                return true;
            }
        }

        if (is_composer_newline_event(event))
        {
            composer_state.insert_newline();
            return true;
        }

        if (is_submit_key(event))
        {
            TuiComposerSubmitResult submit_result;
            {
                std::lock_guard lock{mutex};
                submit_result = model.handle_composer_submit(composer_state.content());
                if (submit_result.composer_changed)
                {
                    composer_state.set_content(model.state().composer);
                }
            }

            if (submit_result.request_model_selector)
            {
                auto options = model_list_provider ? model_list_provider() : std::vector<ModelOption>{};
                std::lock_guard lock{mutex};
                if (!options.empty())
                {
                    model.open_select_modal("Select model", std::move(options));
                }
                else
                {
                    model.append_system_message("No models available.");
                }
                return true;
            }

            if (submit_result.action != TuiAction::SubmitPrompt)
            {
                return true;
            }

            composer_state.push_history(submit_result.prompt);
            if (auto engine_prompt = resolve_submit_prompt(runtime, model, display_config, std::move(submit_result.prompt)))
            {
                run_prompt(std::move(*engine_prompt));
            }
            return true;
        }

        if (event == Event::Character("/") && composer_state.content().empty())
        {
            std::lock_guard lock{mutex};
            if (model.handle_composer_slash_start(command_entries))
            {
                composer_state.set_content(model.state().composer);
                return true;
            }
        }

        if (event.is_mouse())
        {
            const auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp)
            {
                std::lock_guard lock{mutex};
                (void)model.handle_transcript_wheel(true, false);
                return true;
            }
            if (mouse.button == Mouse::WheelDown)
            {
                std::lock_guard lock{mutex};
                (void)model.handle_transcript_wheel(false, true);
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

        const auto terminal_width = std::max(terminal.screen().dimx(), 40);
        Elements rows;

        if (model.state().transcript.empty())
        {
            rows.push_back(render::welcome_banner_element(display_config));
        }
        else
        {
            constexpr auto kFixedComposerRows = 7;
            const auto transcript_height = std::max(1, terminal.screen().dimy() - kFixedComposerRows);
            rows.push_back(render::transcript_view_element(
                model.state().transcript,
                terminal_width,
                transcript_height,
                model.state().follow_transcript));
        }

        if (model.state().bottom_pane_focus == BottomPaneFocus::permission_prompt && model.state().pending_permission)
        {
            rows.push_back(text(" "));
            rows.push_back(render::permission_modal_element(*model.state().pending_permission, terminal_width));
        }
        else if (model.state().bottom_pane_focus == BottomPaneFocus::select_modal && model.state().select_modal)
        {
            rows.push_back(text(" "));
            rows.push_back(render::select_modal_element(*model.state().select_modal, terminal_width));
        }
        else if (model.state().bottom_pane_focus == BottomPaneFocus::question_modal && model.state().question_modal)
        {
            rows.push_back(text(" "));
            rows.push_back(render::question_modal_element(*model.state().question_modal, terminal_width));
        }
        else if (model.state().bottom_pane_focus == BottomPaneFocus::command_palette && model.state().command_palette)
        {
            rows.push_back(text(" "));
            rows.push_back(render::command_palette_element(*model.state().command_palette, terminal_width));
        }

        rows.push_back(text(" "));
        rows.push_back(text(render::horizontal_rule(terminal_width)) | dim);
        rows.push_back(render::status_footer_element(display_config, model.state()));

        if (bottom_pane_accepts_composer_input(model.state().bottom_pane_focus))
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

        return vbox(std::move(rows)) | flex;
    });

    terminal.run(renderer);

    drain_tui_events();

    std::shared_ptr<CancellationSource> source_to_cancel;
    {
        std::lock_guard lock{mutex};
        source_to_cancel = cancellation_source;
        permission_response = PermissionResponse{.allowed = false, .reason = "interrupted"};
        model.handle_interrupt();
    }
    if (source_to_cancel)
    {
        source_to_cancel->cancel();
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

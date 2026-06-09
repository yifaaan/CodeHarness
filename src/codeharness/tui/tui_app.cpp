#include "codeharness/tui/tui_app.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/core/strings.h"
#include "codeharness/tui/tui_composer.h"
#include "codeharness/tui/tui_render.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
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

auto command_matches_query(const CommandPaletteEntry& command, std::string_view query) -> bool
{
    if (query.empty())
    {
        return true;
    }

    const auto contains = [query](const std::string& value) { return value.find(query) != std::string::npos; };
    if (contains(command.name) || contains(command.description))
    {
        return true;
    }

    return std::ranges::any_of(command.aliases, contains);
}

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

auto is_slash_command_prefix(std::string_view input) -> bool
{
    return input.starts_with('/') && input.find_first_of(" \t\r\n") == std::string_view::npos;
}

auto last_transcript_is_error(const TuiState& state, std::string_view text) -> bool
{
    return !state.transcript.empty() && state.transcript.back().kind == "error" && state.transcript.back().text == text;
}

auto find_tool_item(TuiState& state, std::string_view id) -> TranscriptItem*
{
    const auto item = std::ranges::find_if(state.transcript, [id](const TranscriptItem& transcript_item) { return transcript_item.kind == "tool" && transcript_item.id == id; });
    if (item == state.transcript.end())
    {
        return nullptr;
    }
    return &*item;
}

auto update_tool_text(TranscriptItem& item, ToolStatus status, std::string_view detail = {}) -> void
{
    item.tool_status = status;
    item.detail = std::string{detail};
    if (item.label.empty())
    {
        return;
    }
    if (status == ToolStatus::running)
    {
        item.text = item.label + " running";
    }
    else if (status == ToolStatus::failed)
    {
        item.text = item.label + " failed";
    }
    else
    {
        item.text = item.label + " completed";
    }
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

auto resolve_submit_prompt(runtime::RuntimeBundle& runtime, TuiAppModel& model, std::string prompt) -> std::optional<std::string>
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
    if (trimmed == "/mode" || trimmed == "/permissions")
    {
        const auto mode = runtime.permission_mode();
        const auto label = mode == PermissionMode::Plan ? "plan"
                         : mode == PermissionMode::FullAuto ? "full_auto"
                         : "default";
        model.append_system_message("Current permission mode: " + std::string{label});
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

    output << render::render_status_footer_line({}, state_) << '\n';
    output << (state_.busy ? "Working" : "Ready") << '\n';
    output << "> " << state_.composer;
    return output.str();
}

auto TuiAppModel::set_composer(std::string value) -> void
{
    state_.composer = std::move(value);
    update_command_palette_from_composer();
}

auto TuiAppModel::open_command_palette(std::vector<CommandPaletteEntry> commands) -> void
{
    if (state_.busy || state_.pending_permission)
    {
        return;
    }

    state_.command_palette = CommandPaletteState{.commands = std::move(commands)};
    update_command_palette_from_composer();
    refresh_command_palette_matches();
}

auto TuiAppModel::close_command_palette() -> void
{
    state_.command_palette.reset();
}

auto TuiAppModel::update_command_palette_from_composer() -> void
{
    if (!state_.command_palette)
    {
        return;
    }

    if (!is_slash_command_prefix(state_.composer))
    {
        close_command_palette();
        return;
    }

    auto query = std::string_view{state_.composer};
    query.remove_prefix(1);
    state_.command_palette->query = std::string{query};
    refresh_command_palette_matches();
}

auto TuiAppModel::command_palette_input(char character) -> void
{
    if (!state_.command_palette || state_.busy || state_.pending_permission)
    {
        return;
    }

    state_.composer.push_back(character);
    update_command_palette_from_composer();
}

auto TuiAppModel::command_palette_backspace() -> void
{
    if (!state_.command_palette || state_.busy || state_.pending_permission || state_.composer.empty())
    {
        return;
    }

    state_.composer.pop_back();
    if (state_.composer.empty())
    {
        close_command_palette();
        return;
    }

    update_command_palette_from_composer();
}

auto TuiAppModel::command_palette_up() -> void
{
    if (!state_.command_palette || state_.command_palette->matches.empty())
    {
        return;
    }

    auto& cursor = state_.command_palette->cursor;
    cursor = cursor == 0 ? state_.command_palette->matches.size() - 1 : cursor - 1;
}

auto TuiAppModel::command_palette_down() -> void
{
    if (!state_.command_palette || state_.command_palette->matches.empty())
    {
        return;
    }

    auto& cursor = state_.command_palette->cursor;
    cursor = (cursor + 1) % state_.command_palette->matches.size();
}

auto TuiAppModel::selected_command_text() const -> std::optional<std::string>
{
    if (!state_.command_palette || state_.command_palette->matches.empty())
    {
        return std::nullopt;
    }

    const auto index = state_.command_palette->matches.at(state_.command_palette->cursor);
    return "/" + state_.command_palette->commands.at(index).name + " ";
}

auto TuiAppModel::handle_submit() -> TuiAction
{
    if (state_.busy || state_.composer.empty() || state_.pending_permission || state_.command_palette)
    {
        return TuiAction::None;
    }
    return TuiAction::SubmitPrompt;
}

auto TuiAppModel::handle_quit() -> TuiAction
{
    if (state_.busy || state_.pending_permission || state_.command_palette)
    {
        return TuiAction::None;
    }
    state_.should_quit = true;
    return TuiAction::Quit;
}

auto TuiAppModel::handle_command_select() -> TuiAction
{
    auto selected = selected_command_text();
    if (!selected)
    {
        return TuiAction::None;
    }

    state_.composer = std::move(*selected);
    close_command_palette();
    return TuiAction::InsertCommand;
}

auto TuiAppModel::handle_command_cancel() -> TuiAction
{
    if (!state_.command_palette)
    {
        return TuiAction::None;
    }

    if (!state_.command_palette->query.empty())
    {
        state_.composer = "/";
        update_command_palette_from_composer();
        return TuiAction::None;
    }

    close_command_palette();
    return TuiAction::None;
}

auto TuiAppModel::handle_interrupt() -> TuiAction
{
    if (!state_.busy && !state_.pending_permission)
    {
        return TuiAction::None;
    }

    if (!state_.interrupt_requested && !last_transcript_is_error(state_, "interrupted"))
    {
        state_.transcript.push_back(TranscriptItem{.kind = "error", .text = "interrupted", .is_error = true});
    }
    state_.interrupt_requested = true;
    state_.pending_permission.reset();
    state_.command_palette.reset();
    state_.question_modal.reset();
    return TuiAction::Interrupt;
}

auto TuiAppModel::handle_permission_approve() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    state_.pending_permission.reset();
    return TuiAction::ApprovePermission;
}

auto TuiAppModel::handle_permission_approve_for_session() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    state_.pending_permission.reset();
    return TuiAction::ApprovePermissionForSession;
}

auto TuiAppModel::handle_permission_deny() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    state_.pending_permission.reset();
    return TuiAction::DenyPermission;
}

auto TuiAppModel::toggle_tool_details(std::size_t transcript_index) -> bool
{
    if (transcript_index >= state_.transcript.size())
    {
        return false;
    }

    auto& item = state_.transcript.at(transcript_index);
    if (item.kind != "tool" || item.detail.empty())
    {
        return false;
    }

    item.expanded = !item.expanded;
    return true;
}

auto TuiAppModel::begin_prompt(std::string prompt) -> void
{
    state_.transcript.push_back(TranscriptItem{.kind = "user", .text = std::move(prompt)});
    state_.composer.clear();
    state_.command_palette.reset();
    state_.interrupt_requested = false;
    streamed_assistant_output_ = false;
    state_.busy = true;
}

auto TuiAppModel::complete_prompt() -> void
{
    state_.busy = false;
    state_.interrupt_requested = false;
}

auto TuiAppModel::apply_engine_event(const EngineEvent& event) -> void
{
    std::visit(
        Overloaded{
            [this](const EngineAssistantTextDelta& delta) {
                streamed_assistant_output_ = true;
                if (!state_.transcript.empty() && state_.transcript.back().kind == "assistant")
                {
                    state_.transcript.back().text += delta.text;
                    return;
                }
                state_.transcript.push_back(TranscriptItem{.kind = "assistant", .text = delta.text});
            },
            [this](const EngineToolStarted& started) {
                state_.transcript.push_back(
                    TranscriptItem{
                        .kind = "tool",
                        .text = started.name + " running",
                        .id = started.id,
                        .label = started.name,
                        .tool_status = ToolStatus::running,
                    });
            },
            [this](const EngineToolFinished& finished) {
                if (auto* item = find_tool_item(state_, finished.id))
                {
                    update_tool_text(*item, ToolStatus::completed);
                    return;
                }
                state_.transcript.push_back(
                    TranscriptItem{
                        .kind = "tool",
                        .text = "completed " + finished.id,
                        .id = finished.id,
                        .tool_status = ToolStatus::completed,
                    });
            },
            [this](const EngineToolResult& result) {
                if (auto* item = find_tool_item(state_, result.id))
                {
                    item->is_error = result.is_error;
                    item->expanded = result.is_error;
                    update_tool_text(*item, result.is_error ? ToolStatus::failed : ToolStatus::completed, result.content);
                    return;
                }
                state_.transcript.push_back(
                    TranscriptItem{
                        .kind = "tool",
                        .text = result.is_error ? "failed" : "completed",
                        .detail = result.content,
                        .id = result.id,
                        .tool_status = result.is_error ? ToolStatus::failed : ToolStatus::completed,
                        .is_error = result.is_error,
                        .expanded = result.is_error,
                    });
            },
            [this](const EngineError& error) {
                if (last_transcript_is_error(state_, error.message))
                {
                    return;
                }
                state_.transcript.push_back(TranscriptItem{.kind = "error", .text = error.message, .is_error = true});
            },
        },
        event);
}

auto TuiAppModel::has_streamed_assistant_output() const noexcept -> bool
{
    return streamed_assistant_output_;
}

auto TuiAppModel::show_permission(const PermissionPrompt& prompt) -> void
{
    state_.command_palette.reset();
    state_.pending_permission = prompt;
}

auto TuiAppModel::clear_permission() -> void
{
    state_.pending_permission.reset();
}

auto TuiAppModel::append_system_message(std::string text) -> void
{
    if (text.empty())
    {
        return;
    }
    state_.transcript.push_back(TranscriptItem{.kind = "system", .text = std::move(text)});
}

// --- Select modal (/model picker) ---

auto TuiAppModel::open_select_modal(std::string title, std::vector<ModelOption> options) -> void
{
    if (state_.busy || state_.pending_permission)
    {
        return;
    }

    std::size_t initial_cursor = 0;
    for (std::size_t index = 0; index < options.size(); ++index)
    {
        if (options.at(index).is_current)
        {
            initial_cursor = index;
            break;
        }
    }

    state_.select_modal = SelectModalState{
        .title = std::move(title),
        .options = std::move(options),
        .cursor = initial_cursor,
    };
    state_.command_palette.reset();
    state_.question_modal.reset();
}

auto TuiAppModel::close_select_modal() -> void
{
    state_.select_modal.reset();
}

auto TuiAppModel::select_modal_up() -> void
{
    if (!state_.select_modal || state_.select_modal->options.empty())
    {
        return;
    }
    auto& cursor = state_.select_modal->cursor;
    cursor = cursor == 0 ? state_.select_modal->options.size() - 1 : cursor - 1;
}

auto TuiAppModel::select_modal_down() -> void
{
    if (!state_.select_modal || state_.select_modal->options.empty())
    {
        return;
    }
    auto& cursor = state_.select_modal->cursor;
    cursor = (cursor + 1) % state_.select_modal->options.size();
}

auto TuiAppModel::select_modal_current() const -> std::optional<ModelOption>
{
    if (!state_.select_modal || state_.select_modal->options.empty())
    {
        return std::nullopt;
    }
    return state_.select_modal->options.at(state_.select_modal->cursor);
}

auto TuiAppModel::select_modal_quick_select(int digit) -> std::optional<ModelOption>
{
    if (!state_.select_modal)
    {
        return std::nullopt;
    }

    // 1-based digit → 0-based index
    const auto index = static_cast<std::size_t>(digit - 1);
    if (index >= state_.select_modal->options.size())
    {
        return std::nullopt;
    }
    return state_.select_modal->options.at(index);
}

// --- Question modal (AskUser) ---

auto TuiAppModel::show_question(std::string request_id, std::string question, std::string tool_name, std::string reason)
    -> void
{
    state_.question_modal = QuestionModalState{
        .request_id = std::move(request_id),
        .question = std::move(question),
        .tool_name = std::move(tool_name),
        .reason = std::move(reason),
    };
    state_.command_palette.reset();
    state_.select_modal.reset();
}

auto TuiAppModel::close_question() -> void
{
    state_.question_modal.reset();
}

auto TuiAppModel::question_modal_input(char character) -> void
{
    if (!state_.question_modal)
    {
        return;
    }
    state_.question_modal->answer.push_back(character);
}

auto TuiAppModel::question_modal_backspace() -> void
{
    if (!state_.question_modal || state_.question_modal->answer.empty())
    {
        return;
    }
    state_.question_modal->answer.pop_back();
}

auto TuiAppModel::question_modal_newline() -> void
{
    if (!state_.question_modal)
    {
        return;
    }
    state_.question_modal->extra_lines.push_back(state_.question_modal->answer);
    state_.question_modal->answer.clear();
}

auto TuiAppModel::question_modal_submit() -> std::string
{
    if (!state_.question_modal)
    {
        return {};
    }

    auto all_lines = state_.question_modal->extra_lines;
    all_lines.push_back(state_.question_modal->answer);
    std::string result;
    for (std::size_t index = 0; index < all_lines.size(); ++index)
    {
        result += all_lines.at(index);
        if (index + 1 < all_lines.size())
        {
            result.push_back('\n');
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
    // A paste burst is any multi-character input that arrives as a single event,
    // or input containing bracketed paste escape sequences.
    const auto has_bracketed_paste = input.find("\x1b[200~") != std::string::npos ||
                                     input.find("\x1b[201~") != std::string::npos;
    state_.paste_burst_active = input.size() > 1 || has_bracketed_paste;
}

auto TuiAppModel::apply_paste_to_composer(const std::string& paste_text) -> void
{
    if (state_.busy || state_.pending_permission || state_.select_modal || state_.question_modal)
    {
        return;
    }

    // Strip bracketed paste markers if present
    auto text = paste_text;
    auto strip_marker = [](std::string& s, std::string_view marker) {
        auto pos = s.find(marker);
        while (pos != std::string::npos)
        {
            s.erase(pos, marker.size());
            pos = s.find(marker, pos);
        }
    };
    strip_marker(text, "\x1b[200~");
    strip_marker(text, "\x1b[201~");

    state_.composer += text;
    update_command_palette_from_composer();
}

auto TuiAppModel::refresh_command_palette_matches() -> void
{
    if (!state_.command_palette)
    {
        return;
    }

    auto& palette = *state_.command_palette;
    palette.matches.clear();
    for (std::size_t index = 0; index < palette.commands.size(); ++index)
    {
        if (command_matches_query(palette.commands.at(index), palette.query))
        {
            palette.matches.push_back(index);
        }
    }

    if (palette.cursor >= palette.matches.size())
    {
        palette.cursor = 0;
    }
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
    bool follow_transcript = true;
    int spinner_frame = 0;
    int last_transcript_count = 0;

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

        worker = std::thread{[&, prompt = std::move(prompt), cancellation, screen_alive] {
            const auto post_refresh = [&] {
                if (screen_alive->load(std::memory_order_acquire))
                {
                    screen.PostEvent(Event::Custom);
                }
            };
            const auto complete_with_error = [&](std::string message) {
                {
                    std::lock_guard lock{mutex};
                    model.apply_engine_event(EngineError{.message = std::move(message)});
                    model.clear_permission();
                    model.close_question();
                    model.complete_prompt();
                    model.set_permission_mode(runtime.permission_mode());
                    model.set_active_session(runtime.active_session_summary());
                    permission_response.reset();
                    user_question_response.reset();
                    cancellation_source.reset();
                    worker_finished = true;
                }
                post_refresh();
            };

            try
            {
                auto result = runtime.run_prompt(
                    prompt,
                    runtime::RunPromptOptions{
                        .max_turns = max_turns,
                        .permission_prompt = [&, screen_alive](const PermissionPrompt& permission_prompt) -> Result<PermissionResponse> {
                            {
                                std::lock_guard lock{mutex};
                                permission_response.reset();
                                model.show_permission(permission_prompt);
                            }
                            if (screen_alive->load(std::memory_order_acquire))
                            {
                                screen.PostEvent(Event::Custom);
                            }

                            {
                                std::unique_lock lock{mutex};
                                permission_cv.wait(lock, [&] { return permission_response.has_value() || model.state().interrupt_requested; });
                                auto response = permission_response.value_or(PermissionResponse{.allowed = false, .reason = "interrupted"});
                                permission_response.reset();
                                model.clear_permission();
                                lock.unlock();

                                if (screen_alive->load(std::memory_order_acquire))
                                {
                                    screen.PostEvent(Event::Custom);
                                }
                                return response;
                            }
                        },
                        .user_question = [&, screen_alive](const UserQuestionPrompt& question_prompt) -> Result<UserQuestionResponse> {
                            {
                                std::lock_guard lock{mutex};
                                user_question_response.reset();
                                model.show_question(
                                    question_prompt.id,
                                    question_prompt.question,
                                    "ask_user",
                                    question_prompt.reason);
                            }
                            if (screen_alive->load(std::memory_order_acquire))
                            {
                                screen.PostEvent(Event::Custom);
                            }

                            {
                                std::unique_lock lock{mutex};
                                user_question_cv.wait(lock, [&] {
                                    return user_question_response.has_value() || model.state().interrupt_requested;
                                });
                                if (!user_question_response)
                                {
                                    model.close_question();
                                    lock.unlock();
                                    if (screen_alive->load(std::memory_order_acquire))
                                    {
                                        screen.PostEvent(Event::Custom);
                                    }
                                    return fail<UserQuestionResponse>(ErrorKind::Cancelled, "interrupted");
                                }

                                auto response = *user_question_response;
                                user_question_response.reset();
                                model.close_question();
                                lock.unlock();

                                if (screen_alive->load(std::memory_order_acquire))
                                {
                                    screen.PostEvent(Event::Custom);
                                }
                                return response;
                            }
                        },
                        .cancellation = cancellation,
                    },
                    [&, screen_alive](const EngineEvent& event) {
                        {
                            std::lock_guard lock{mutex};
                            model.apply_engine_event(event);
                        }
                        if (screen_alive->load(std::memory_order_acquire))
                        {
                            screen.PostEvent(Event::Custom);
                        }
                    });

                {
                    std::lock_guard lock{mutex};
                    if (!result)
                    {
                        model.apply_engine_event(EngineError{.message = result.error().message});
                    }
                    else if (!result->output_text.empty() && !model.has_streamed_assistant_output())
                    {
                        model.apply_engine_event(EngineAssistantTextDelta{.text = result->output_text});
                    }
                    if (result)
                    {
                        display_config.token_usage = TokenUsage{
                            .input_tokens = result->usage.input_tokens,
                            .output_tokens = result->usage.output_tokens,
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
                }
                post_refresh();
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
                        model_select_callback(*selected);
                        display_config.model = selected->value;
                        model.append_system_message("Switched model to " + selected->label);
                    }
                    return true;
                }
                if (event == Event::Escape)
                {
                    model.close_select_modal();
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
                                    model_select_callback(*selected);
                                    display_config.model = selected->value;
                                    model.append_system_message("Switched model to " + selected->label);
                                }
                            }
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

            if (auto engine_prompt = resolve_submit_prompt(runtime, model, std::move(prompt)))
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
                follow_transcript = false;
                return true;
            }
            if (mouse.button == Mouse::WheelDown)
            {
                return true;
            }
        }

        return false;
    });

    auto renderer = Renderer(component, [&] {
        std::lock_guard lock{mutex};

        if (model.state().busy)
        {
            spinner_frame = (spinner_frame + 1) % 10;
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

    std::cout << '\n';
    return 0;
}

} // namespace codeharness::tui

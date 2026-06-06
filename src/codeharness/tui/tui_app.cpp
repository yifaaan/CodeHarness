#include "codeharness/tui/tui_app.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/overloaded.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace codeharness::tui
{

namespace
{

auto trim_to_width(std::string text, int width) -> std::string
{
    if (width > 0 && static_cast<int>(text.size()) > width)
    {
        text.resize(static_cast<std::size_t>(width));
    }
    return text;
}

auto prompt_detail_text(const PermissionPrompt& prompt) -> std::string
{
    std::ostringstream output;
    output << prompt.tool_name << ": " << prompt.reason;
    if (prompt.command)
    {
        output << "\ncommand: " << *prompt.command;
    }
    if (prompt.path)
    {
        output << "\npath: " << prompt.path->string();
    }
    return output.str();
}

auto command_matches_query(const CommandPaletteEntry& command, std::string_view query) -> bool
{
    if (query.empty())
    {
        return true;
    }

    const auto contains = [query](const std::string& value) {
        return value.find(query) != std::string::npos;
    };
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
        entries.push_back(CommandPaletteEntry{
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

} // namespace

auto TuiAppModel::state() const noexcept -> const TuiState&
{
    return state_;
}

auto TuiAppModel::render_text(int width) const -> std::string
{
    std::ostringstream output;
    output << "CodeHarness\n";
    for (const auto& item : state_.transcript)
    {
        output << item.kind << ": " << trim_to_width(item.text, width) << '\n';
    }
    if (state_.pending_permission)
    {
        output << "Permission required\n";
        output << prompt_detail_text(*state_.pending_permission) << '\n';
        output << "A approve - D deny\n";
    }
    if (state_.command_palette)
    {
        output << "Select a command";
        if (state_.command_palette->query.empty())
        {
            output << "  (type to search)";
        }
        output << "\nUp/Down navigate - Enter select - Esc cancel\n";
        if (!state_.command_palette->query.empty())
        {
            output << "Search: " << state_.command_palette->query << '\n';
        }
        for (std::size_t index = 0; index < state_.command_palette->matches.size(); ++index)
        {
            const auto command_index = state_.command_palette->matches.at(index);
            const auto& command = state_.command_palette->commands.at(command_index);
            output << (index == state_.command_palette->cursor ? "> " : "  ");
            output << "/" << command.name << "  " << command.description << '\n';
        }
        if (state_.command_palette->matches.empty())
        {
            output << "No matches\n";
        }
    }
    output << (state_.busy ? "Running" : "Ready") << "\n> " << state_.composer;
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

auto TuiAppModel::handle_permission_approve() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    return TuiAction::ApprovePermission;
}

auto TuiAppModel::handle_permission_deny() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    return TuiAction::DenyPermission;
}

auto TuiAppModel::begin_prompt(std::string prompt) -> void
{
    state_.transcript.push_back(TranscriptItem{.kind = "user", .text = std::move(prompt)});
    state_.composer.clear();
    state_.command_palette.reset();
    state_.busy = true;
}

auto TuiAppModel::complete_prompt() -> void
{
    state_.busy = false;
}

auto TuiAppModel::apply_engine_event(const EngineEvent& event) -> void
{
    std::visit(
        Overloaded{
            [this](const EngineAssistantTextDelta& delta) {
                if (!state_.transcript.empty() && state_.transcript.back().kind == "assistant")
                {
                    state_.transcript.back().text += delta.text;
                    return;
                }
                state_.transcript.push_back(TranscriptItem{.kind = "assistant", .text = delta.text});
            },
            [this](const EngineToolStarted& started) {
                state_.transcript.push_back(TranscriptItem{.kind = "tool", .text = "started " + started.name});
            },
            [this](const EngineToolFinished& finished) {
                state_.transcript.push_back(TranscriptItem{.kind = "tool", .text = "completed " + finished.id});
            },
            [this](const EngineToolResult& result) {
                state_.transcript.push_back(
                    TranscriptItem{.kind = "tool_result", .text = result.content, .is_error = result.is_error});
            },
            [this](const EngineError& error) {
                state_.transcript.push_back(TranscriptItem{.kind = "error", .text = error.message, .is_error = true});
            },
        },
        event);
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

auto run_tui(runtime::RuntimeBundle& runtime, int max_turns) -> Result<int>
{
    using namespace ftxui;

    auto screen = ScreenInteractive::TerminalOutput();
    TuiAppModel model;
    std::string input;
    auto command_entries = command_entries_from_registry(runtime.commands());

    std::mutex mutex;
    std::condition_variable permission_cv;
    std::optional<PermissionResponse> permission_response;

    auto run_prompt = [&](std::string prompt) {
        model.begin_prompt(prompt);
        std::thread worker{[&, prompt = std::move(prompt)] {
            auto result = runtime.run_prompt(
                prompt,
                runtime::RunPromptOptions{
                    .max_turns = max_turns,
                    .permission_prompt =
                        [&](const PermissionPrompt& permission_prompt) -> Result<PermissionResponse> {
                        {
                            std::lock_guard lock{mutex};
                            permission_response.reset();
                            model.show_permission(permission_prompt);
                        }
                        screen.PostEvent(Event::Custom);

                        std::unique_lock lock{mutex};
                        permission_cv.wait(lock, [&] { return permission_response.has_value(); });
                        auto response = *permission_response;
                        permission_response.reset();
                        model.clear_permission();
                        screen.PostEvent(Event::Custom);
                        return response;
                    },
                },
                [&](const EngineEvent& event) {
                    std::lock_guard lock{mutex};
                    model.apply_engine_event(event);
                    screen.PostEvent(Event::Custom);
                });

            {
                std::lock_guard lock{mutex};
                if (!result)
                {
                    model.apply_engine_event(EngineError{.message = result.error().message});
                }
                model.complete_prompt();
            }
            screen.PostEvent(Event::Custom);
        }};
        worker.detach();
    };

    auto input_component = Input(&input, "Ask CodeHarness");
    auto component = CatchEvent(input_component, [&](Event event) {
        {
            std::lock_guard lock{mutex};
            if (model.state().pending_permission)
            {
                if (event == Event::Character("a") || event == Event::Character("A"))
                {
                    permission_response = PermissionResponse{.allowed = true};
                    permission_cv.notify_one();
                    return true;
                }
                if (event == Event::Character("d") || event == Event::Character("D") || event == Event::Escape)
                {
                    permission_response = PermissionResponse{.allowed = false, .reason = "user denied permission"};
                    permission_cv.notify_one();
                    return true;
                }
                return true;
            }
        }

        if (event == Event::CtrlC || event == Event::CtrlD)
        {
            std::lock_guard lock{mutex};
            if (model.handle_quit() == TuiAction::Quit)
            {
                screen.ExitLoopClosure()();
            }
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
                    input = model.state().composer;
                    return true;
                }
                if (event == Event::Backspace)
                {
                    model.command_palette_backspace();
                    input = model.state().composer;
                    return true;
                }
                if (event == Event::Return)
                {
                    model.handle_command_select();
                    input = model.state().composer;
                    return true;
                }
                if (event.is_character())
                {
                    const auto characters = event.input();
                    if (characters.size() == 1 && std::isprint(static_cast<unsigned char>(characters.front())) != 0)
                    {
                        model.command_palette_input(characters.front());
                        input = model.state().composer;
                        return true;
                    }
                }
            }
        }

        if (event == Event::Return)
        {
            {
                std::lock_guard lock{mutex};
                model.set_composer(input);
                if (model.handle_submit() != TuiAction::SubmitPrompt)
                {
                    return true;
                }
            }
            auto prompt = input;
            input.clear();
            run_prompt(std::move(prompt));
            return true;
        }

        if (event == Event::Character("/") && input.empty())
        {
            std::lock_guard lock{mutex};
            input = "/";
            model.set_composer(input);
            model.open_command_palette(command_entries);
            return true;
        }

        return false;
    });

    auto renderer = Renderer(component, [&] {
        std::lock_guard lock{mutex};

        Elements rows;
        rows.push_back(text("CodeHarness") | bold);
        rows.push_back(separator());
        for (const auto& item : model.state().transcript)
        {
            auto row = text(item.kind + ": " + item.text);
            rows.push_back(item.is_error ? row | color(Color::Red) : row);
        }
        rows.push_back(filler());

        if (model.state().pending_permission)
        {
            const auto& prompt = *model.state().pending_permission;
            rows.push_back(separator());
            rows.push_back(text("Permission required") | bold | color(Color::Yellow));
            rows.push_back(text(prompt.tool_name + ": " + prompt.reason));
            if (prompt.command)
            {
                rows.push_back(text("command: " + *prompt.command));
            }
            if (prompt.path)
            {
                rows.push_back(text("path: " + prompt.path->string()));
            }
            rows.push_back(text("A approve - D deny") | dim);
        }
        else if (model.state().command_palette)
        {
            const auto& palette = *model.state().command_palette;
            rows.push_back(separator());
            rows.push_back(text(palette.query.empty() ? "Select a command  (type to search)" : "Select a command") |
                           bold);
            rows.push_back(text(palette.query.empty() ? "Up/Down navigate - Enter select - Esc cancel"
                                                      : "Up/Down navigate - Enter select - Esc cancel - Backspace clear") |
                           dim);
            if (!palette.query.empty())
            {
                rows.push_back(text("Search: " + palette.query));
            }
            if (palette.matches.empty())
            {
                rows.push_back(text("No matches") | dim);
            }
            for (std::size_t row = 0; row < palette.matches.size() && row < 8; ++row)
            {
                const auto command_index = palette.matches.at(row);
                const auto& command = palette.commands.at(command_index);
                auto line = std::string{row == palette.cursor ? "> /" : "  /"} + command.name + "  " +
                            command.description;
                auto rendered = text(line);
                rows.push_back(row == palette.cursor ? rendered | bold | color(Color::Cyan) : rendered);
            }
            if (palette.matches.size() > 8)
            {
                rows.push_back(text("v " + std::to_string(palette.matches.size() - 8) + " more") | dim);
            }
        }

        rows.push_back(separator());
        rows.push_back(hbox({text(model.state().busy ? "Running " : "Ready "), input_component->Render()}));
        return vbox(std::move(rows)) | border;
    });

    screen.Loop(renderer);
    return 0;
}

} // namespace codeharness::tui

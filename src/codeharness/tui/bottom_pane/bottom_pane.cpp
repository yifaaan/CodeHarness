#include "codeharness/tui/bottom_pane/bottom_pane.h"

#include "codeharness/tui/selection_list.h"

#include <algorithm>
#include <string_view>
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

auto model_option_matches_query(const ModelOption& option, std::string_view query) -> bool
{
    if (query.empty())
    {
        return true;
    }

    const auto contains = [query](const std::string& value) { return value.find(query) != std::string::npos; };
    return contains(option.value) || contains(option.label) || contains(option.description);
}

auto is_slash_command_prefix(std::string_view input) -> bool
{
    return input.starts_with('/') && input.find_first_of(" \t\r\n") == std::string_view::npos;
}

auto strip_marker(std::string& value, std::string_view marker) -> void
{
    auto pos = value.find(marker);
    while (pos != std::string::npos)
    {
        value.erase(pos, marker.size());
        pos = value.find(marker, pos);
    }
}

auto current_focus(const BottomPaneState& state) -> BottomPaneFocus
{
    if (state.pending_permission)
    {
        return BottomPaneFocus::permission_prompt;
    }
    if (state.question_modal)
    {
        return BottomPaneFocus::question_modal;
    }
    if (state.select_modal)
    {
        return BottomPaneFocus::select_modal;
    }
    if (state.command_palette)
    {
        return BottomPaneFocus::command_palette;
    }
    return BottomPaneFocus::composer;
}

} // namespace

auto bottom_pane_accepts_composer_input(BottomPaneFocus focus) -> bool
{
    return focus == BottomPaneFocus::composer || focus == BottomPaneFocus::command_palette;
}

auto BottomPane::state() const noexcept -> const BottomPaneState&
{
    return state_;
}

auto BottomPane::set_composer(std::string value) -> void
{
    state_.composer = std::move(value);
    update_command_palette_from_composer();
}

auto BottomPane::clear_prompt_entry() -> void
{
    state_.composer.clear();
    state_.command_palette.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::can_submit_prompt() const -> bool
{
    return !state_.composer.empty() && state_.focus == BottomPaneFocus::composer;
}

auto BottomPane::can_quit() const -> bool
{
    return state_.focus == BottomPaneFocus::composer;
}

auto BottomPane::has_pending_permission() const -> bool
{
    return state_.pending_permission.ok();
}

auto BottomPane::clear_for_interrupt() -> void
{
    state_.pending_permission.reset();
    state_.command_palette.reset();
    state_.select_modal.reset();
    state_.question_modal.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::open_command_palette(std::vector<CommandPaletteEntry> commands, bool busy) -> void
{
    if (busy || !bottom_pane_accepts_composer_input(state_.focus))
    {
        return;
    }

    state_.command_palette = CommandPaletteState{.commands = std::move(commands)};
    state_.focus = BottomPaneFocus::command_palette;
    update_command_palette_from_composer();
    refresh_command_palette_matches();
}

auto BottomPane::close_command_palette() -> void
{
    state_.command_palette.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::update_command_palette_from_composer() -> void
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
    state_.focus = BottomPaneFocus::command_palette;
    refresh_command_palette_matches();
}

auto BottomPane::command_palette_input(char character, bool busy) -> void
{
    if (!state_.command_palette || busy || state_.focus != BottomPaneFocus::command_palette)
    {
        return;
    }

    state_.composer.push_back(character);
    update_command_palette_from_composer();
}

auto BottomPane::command_palette_backspace(bool busy) -> void
{
    if (!state_.command_palette || busy || state_.focus != BottomPaneFocus::command_palette || state_.composer.empty())
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

auto BottomPane::command_palette_up() -> void
{
    if (!state_.command_palette)
    {
        return;
    }

    state_.command_palette->cursor =
        move_selection_up(state_.command_palette->cursor, state_.command_palette->matches.size());
}

auto BottomPane::command_palette_down() -> void
{
    if (!state_.command_palette)
    {
        return;
    }

    state_.command_palette->cursor =
        move_selection_down(state_.command_palette->cursor, state_.command_palette->matches.size());
}

auto BottomPane::selected_command_text() const -> std::optional<std::string>
{
    if (!state_.command_palette)
    {
        return std::nullopt;
    }

    const auto index = selected_match_index(state_.command_palette->matches, state_.command_palette->cursor);
    if (!index)
    {
        return std::nullopt;
    }
    return "/" + state_.command_palette->commands.at(*index).name + " ";
}

auto BottomPane::handle_command_select() -> bool
{
    auto selected = selected_command_text();
    if (!selected)
    {
        return false;
    }

    state_.composer = std::move(*selected);
    close_command_palette();
    return true;
}

auto BottomPane::handle_command_cancel() -> void
{
    if (!state_.command_palette)
    {
        return;
    }

    if (!state_.command_palette->query.empty())
    {
        state_.composer = "/";
        update_command_palette_from_composer();
        return;
    }

    close_command_palette();
}

auto BottomPane::show_permission(const PermissionPrompt& prompt) -> void
{
    state_.command_palette.reset();
    state_.select_modal.reset();
    state_.pending_permission = prompt;
    state_.focus = current_focus(state_);
}

auto BottomPane::clear_permission() -> void
{
    state_.pending_permission.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::handle_permission_approve() -> bool
{
    if (!state_.pending_permission)
    {
        return false;
    }
    state_.pending_permission.reset();
    state_.focus = current_focus(state_);
    return true;
}

auto BottomPane::handle_permission_approve_for_session() -> bool
{
    if (!state_.pending_permission)
    {
        return false;
    }
    state_.pending_permission.reset();
    state_.focus = current_focus(state_);
    return true;
}

auto BottomPane::handle_permission_deny() -> bool
{
    if (!state_.pending_permission)
    {
        return false;
    }
    state_.pending_permission.reset();
    state_.focus = current_focus(state_);
    return true;
}

auto BottomPane::open_select_modal(std::string title, std::vector<ModelOption> options, bool busy) -> void
{
    if (busy || !bottom_pane_accepts_composer_input(state_.focus))
    {
        return;
    }

    state_.select_modal = SelectModalState{
        .title = std::move(title),
        .options = std::move(options),
        .is_searchable = true,
    };
    refresh_select_modal_matches();
    if (state_.select_modal)
    {
        for (std::size_t match_index = 0; match_index < state_.select_modal->matches.size(); ++match_index)
        {
            if (state_.select_modal->options.at(state_.select_modal->matches.at(match_index)).is_current)
            {
                state_.select_modal->cursor = match_index;
                break;
            }
        }
    }
    state_.command_palette.reset();
    state_.question_modal.reset();
    state_.focus = BottomPaneFocus::select_modal;
}

auto BottomPane::close_select_modal() -> void
{
    state_.select_modal.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::select_modal_up() -> void
{
    if (!state_.select_modal)
    {
        return;
    }
    state_.select_modal->cursor = move_selection_up(state_.select_modal->cursor, state_.select_modal->matches.size());
}

auto BottomPane::select_modal_down() -> void
{
    if (!state_.select_modal)
    {
        return;
    }
    state_.select_modal->cursor =
        move_selection_down(state_.select_modal->cursor, state_.select_modal->matches.size());
}

auto BottomPane::select_modal_input(char character, bool busy) -> void
{
    if (!state_.select_modal || !state_.select_modal->is_searchable || busy || state_.focus != BottomPaneFocus::select_modal)
    {
        return;
    }

    state_.select_modal->query.push_back(character);
    refresh_select_modal_matches();
}

auto BottomPane::select_modal_backspace(bool busy) -> void
{
    if (!state_.select_modal || !state_.select_modal->is_searchable || busy
        || state_.focus != BottomPaneFocus::select_modal || state_.select_modal->query.empty())
    {
        return;
    }

    state_.select_modal->query.pop_back();
    refresh_select_modal_matches();
}

auto BottomPane::handle_select_cancel() -> void
{
    if (!state_.select_modal)
    {
        return;
    }

    if (!state_.select_modal->query.empty())
    {
        state_.select_modal->query.clear();
        refresh_select_modal_matches();
        return;
    }

    close_select_modal();
}

auto BottomPane::select_modal_current() const -> std::optional<ModelOption>
{
    if (!state_.select_modal)
    {
        return std::nullopt;
    }
    const auto index = selected_match_index(state_.select_modal->matches, state_.select_modal->cursor);
    if (!index)
    {
        return std::nullopt;
    }
    return state_.select_modal->options.at(*index);
}

auto BottomPane::select_modal_quick_select(int digit) const -> std::optional<ModelOption>
{
    if (!state_.select_modal)
    {
        return std::nullopt;
    }

    const auto index = static_cast<std::size_t>(digit - 1);
    const auto selected_index = selected_match_index(state_.select_modal->matches, index);
    if (!selected_index)
    {
        return std::nullopt;
    }
    return state_.select_modal->options.at(*selected_index);
}

auto BottomPane::show_question(std::string request_id, std::string question, std::string tool_name, std::string reason)
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
    state_.focus = current_focus(state_);
}

auto BottomPane::close_question() -> void
{
    state_.question_modal.reset();
    state_.focus = current_focus(state_);
}

auto BottomPane::question_modal_input(char character) -> void
{
    if (!state_.question_modal || state_.focus != BottomPaneFocus::question_modal)
    {
        return;
    }
    state_.question_modal->answer.push_back(character);
}

auto BottomPane::question_modal_backspace() -> void
{
    if (!state_.question_modal || state_.focus != BottomPaneFocus::question_modal || state_.question_modal->answer.empty())
    {
        return;
    }
    state_.question_modal->answer.pop_back();
}

auto BottomPane::question_modal_newline() -> void
{
    if (!state_.question_modal || state_.focus != BottomPaneFocus::question_modal)
    {
        return;
    }
    state_.question_modal->extra_lines.push_back(state_.question_modal->answer);
    state_.question_modal->answer.clear();
}

auto BottomPane::question_modal_submit() const -> std::string
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

auto BottomPane::detect_paste_burst(const std::string& input) -> void
{
    const auto has_bracketed_paste = input.find("\x1b[200~") != std::string::npos
                                  || input.find("\x1b[201~") != std::string::npos;
    state_.paste_burst_active = input.size() > 1 || has_bracketed_paste;
}

auto BottomPane::apply_paste_to_composer(const std::string& paste_text, bool busy) -> void
{
    if (busy || !bottom_pane_accepts_composer_input(state_.focus))
    {
        return;
    }

    auto text = paste_text;
    strip_marker(text, "\x1b[200~");
    strip_marker(text, "\x1b[201~");

    state_.composer += text;
    update_command_palette_from_composer();
}

auto BottomPane::refresh_command_palette_matches() -> void
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

    palette.cursor = clamp_selection_cursor(palette.cursor, palette.matches.size());
}

auto BottomPane::refresh_select_modal_matches() -> void
{
    if (!state_.select_modal)
    {
        return;
    }

    auto& modal = *state_.select_modal;
    modal.matches.clear();
    for (std::size_t index = 0; index < modal.options.size(); ++index)
    {
        if (model_option_matches_query(modal.options.at(index), modal.query))
        {
            modal.matches.push_back(index);
        }
    }

    modal.cursor = clamp_selection_cursor(modal.cursor, modal.matches.size());
}

} // namespace codeharness::tui

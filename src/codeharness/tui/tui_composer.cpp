#include "codeharness/tui/tui_composer.h"

#include "codeharness/tui/tui_theme.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>

namespace codeharness::tui
{
namespace
{

using namespace ftxui;

auto is_printable_character(const Event& event) -> bool
{
    if (!event.is_character())
    {
        return false;
    }
    const auto characters = event.input();
    return characters.size() == 1 && std::isprint(static_cast<unsigned char>(characters.front())) != 0;
}

} // namespace

auto ComposerState::content() const noexcept -> const std::string&
{
    return content_;
}

auto ComposerState::lines() const -> std::vector<std::string>
{
    return lines_;
}

auto ComposerState::cursor() const noexcept -> ComposerCursor
{
    return cursor_;
}

auto ComposerState::history_index() const noexcept -> int
{
    return history_index_;
}

auto ComposerState::set_content(std::string value) -> void
{
    content_ = std::move(value);
    sync_lines_from_content();
    cursor_.line = lines_.empty() ? 0 : lines_.size() - 1;
    cursor_.column = lines_.empty() ? 0 : lines_.back().size();
    clamp_cursor();
}

auto ComposerState::clear() -> void
{
    content_.clear();
    lines_ = {""};
    cursor_ = {};
    history_index_ = -1;
    history_draft_.clear();
}

auto ComposerState::insert_character(char character) -> void
{
    history_index_ = -1;
    history_draft_.clear();
    auto& line = lines_.at(cursor_.line);
    line.insert(line.begin() + static_cast<std::ptrdiff_t>(cursor_.column), character);
    ++cursor_.column;
    sync_content_from_lines();
}

auto ComposerState::insert_newline() -> void
{
    history_index_ = -1;
    history_draft_.clear();
    auto& line = lines_.at(cursor_.line);
    const auto tail = line.substr(cursor_.column);
    line.resize(cursor_.column);
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(cursor_.line + 1), tail);
    ++cursor_.line;
    cursor_.column = 0;
    sync_content_from_lines();
}

auto ComposerState::backspace() -> void
{
    history_index_ = -1;
    history_draft_.clear();
    if (cursor_.column > 0)
    {
        auto& line = lines_.at(cursor_.line);
        line.erase(line.begin() + static_cast<std::ptrdiff_t>(cursor_.column - 1));
        --cursor_.column;
    }
    else if (cursor_.line > 0)
    {
        const auto previous_length = lines_.at(cursor_.line - 1).size();
        lines_.at(cursor_.line - 1) += lines_.at(cursor_.line);
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(cursor_.line));
        --cursor_.line;
        cursor_.column = previous_length;
    }
    if (lines_.empty())
    {
        lines_.push_back("");
    }
    sync_content_from_lines();
    clamp_cursor();
}

auto ComposerState::move_left() -> void
{
    if (cursor_.column > 0)
    {
        --cursor_.column;
        return;
    }
    if (cursor_.line > 0)
    {
        --cursor_.line;
        cursor_.column = lines_.at(cursor_.line).size();
    }
}

auto ComposerState::move_right() -> void
{
    const auto& line = lines_.at(cursor_.line);
    if (cursor_.column < line.size())
    {
        ++cursor_.column;
        return;
    }
    if (cursor_.line + 1 < lines_.size())
    {
        ++cursor_.line;
        cursor_.column = 0;
    }
}

auto ComposerState::move_up() -> void
{
    if (cursor_.line == 0)
    {
        return;
    }
    --cursor_.line;
    clamp_cursor();
}

auto ComposerState::move_down() -> void
{
    if (cursor_.line + 1 >= lines_.size())
    {
        return;
    }
    ++cursor_.line;
    clamp_cursor();
}

auto ComposerState::history_previous() -> void
{
    if (history_.empty())
    {
        return;
    }

    if (history_index_ < 0)
    {
        history_draft_ = content_;
        history_index_ = static_cast<int>(history_.size()) - 1;
    }
    else if (history_index_ > 0)
    {
        --history_index_;
    }
    else
    {
        return;
    }

    set_content(history_.at(static_cast<std::size_t>(history_index_)));
}

auto ComposerState::history_next() -> void
{
    if (history_index_ < 0)
    {
        return;
    }

    if (history_index_ + 1 < static_cast<int>(history_.size()))
    {
        ++history_index_;
        set_content(history_.at(static_cast<std::size_t>(history_index_)));
        return;
    }

    history_index_ = -1;
    set_content(history_draft_);
    history_draft_.clear();
}

auto ComposerState::push_history(std::string submitted) -> void
{
    if (submitted.empty())
    {
        return;
    }
    if (!history_.empty() && history_.back() == submitted)
    {
        return;
    }
    history_.push_back(std::move(submitted));
    history_index_ = -1;
    history_draft_.clear();
}

auto ComposerState::sync_lines_from_content() -> void
{
    lines_.clear();
    if (content_.empty())
    {
        lines_.push_back("");
        return;
    }

    std::size_t start = 0;
    while (start <= content_.size())
    {
        const auto end = content_.find('\n', start);
        if (end == std::string::npos)
        {
            lines_.push_back(content_.substr(start));
            break;
        }
        lines_.push_back(content_.substr(start, end - start));
        start = end + 1;
    }
    if (lines_.empty())
    {
        lines_.push_back("");
    }
}

auto ComposerState::sync_content_from_lines() -> void
{
    content_.clear();
    for (std::size_t index = 0; index < lines_.size(); ++index)
    {
        content_ += lines_.at(index);
        if (index + 1 < lines_.size())
        {
            content_.push_back('\n');
        }
    }
}

auto ComposerState::clamp_cursor() -> void
{
    if (lines_.empty())
    {
        lines_.push_back("");
    }
    cursor_.line = std::min(cursor_.line, lines_.size() - 1);
    cursor_.column = std::min(cursor_.column, lines_.at(cursor_.line).size());
}

auto is_composer_newline_event(const Event& event) -> bool
{
    const auto& input = event.input();
    return input.find("13;2") != std::string::npos || input.find("27;2~") != std::string::npos;
}

auto make_multiline_composer(ComposerState& state) -> Component
{
    class ComposerComponent : public ComponentBase
    {
    public:
        explicit ComposerComponent(ComposerState& state) : state_{state}
        {
        }

        auto OnEvent(Event event) -> bool override
        {
            if (event == Event::ArrowLeft)
            {
                state_.move_left();
                return true;
            }
            if (event == Event::ArrowRight)
            {
                state_.move_right();
                return true;
            }
            if (event == Event::ArrowUp)
            {
                state_.move_up();
                return true;
            }
            if (event == Event::ArrowDown)
            {
                state_.move_down();
                return true;
            }
            if (event == Event::Backspace)
            {
                state_.backspace();
                return true;
            }
            if (event == Event::CtrlP)
            {
                state_.history_previous();
                return true;
            }
            if (event == Event::CtrlN)
            {
                state_.history_next();
                return true;
            }
            if (is_printable_character(event))
            {
                state_.insert_character(event.input().front());
                return true;
            }
            return false;
        }

        auto OnRender() -> Element override
        {
            const auto& lines = state_.lines();
            const auto& cursor = state_.cursor();
            Elements rows;
            if (lines.empty())
            {
                rows.push_back(text(" ") | color(TuiTheme::text_muted()));
                return vbox(std::move(rows));
            }

            for (std::size_t line_index = 0; line_index < lines.size(); ++line_index)
            {
                const auto& line = lines.at(line_index);
                if (line_index != cursor.line)
                {
                    rows.push_back(text(line.empty() ? " " : line));
                    continue;
                }
                if (cursor.column >= line.size())
                {
                    rows.push_back(hbox({text(line), text(" ") | inverted}));
                    continue;
                }
                rows.push_back(hbox({
                    text(line.substr(0, cursor.column)),
                    text(std::string(1, line.at(cursor.column))) | inverted,
                    text(line.substr(cursor.column + 1)),
                }));
            }
            return vbox(std::move(rows));
        }

    private:
        ComposerState& state_;
    };

    return Make<ComposerComponent>(state);
}

} // namespace codeharness::tui

#pragma once

#include <ftxui/component/component.hpp>

#include <string>
#include <vector>

namespace codeharness::tui
{

struct ComposerCursor
{
    std::size_t line = 0;
    std::size_t column = 0;
};

class ComposerState
{
public:
    [[nodiscard]] auto content() const noexcept -> const std::string&;
    [[nodiscard]] auto lines() const -> std::vector<std::string>;
    [[nodiscard]] auto cursor() const noexcept -> ComposerCursor;
    [[nodiscard]] auto history_index() const noexcept -> int;

    auto set_content(std::string value) -> void;
    auto clear() -> void;
    auto insert_character(char character) -> void;
    auto insert_newline() -> void;
    auto backspace() -> void;
    auto move_left() -> void;
    auto move_right() -> void;
    auto move_up() -> void;
    auto move_down() -> void;
    auto history_previous() -> void;
    auto history_next() -> void;
    auto push_history(std::string submitted) -> void;

private:
    auto sync_lines_from_content() -> void;
    auto sync_content_from_lines() -> void;
    auto clamp_cursor() -> void;

    std::string content_;
    std::vector<std::string> lines_{""};
    ComposerCursor cursor_;
    std::vector<std::string> history_;
    int history_index_ = -1;
    std::string history_draft_;
};

[[nodiscard]] auto make_multiline_composer(ComposerState& state) -> ftxui::Component;
[[nodiscard]] auto is_composer_newline_event(const ftxui::Event& event) -> bool;

} // namespace codeharness::tui

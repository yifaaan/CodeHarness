#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace codeharness::tui
{

[[nodiscard]] auto move_selection_up(std::size_t cursor, std::size_t match_count) -> std::size_t;
[[nodiscard]] auto move_selection_down(std::size_t cursor, std::size_t match_count) -> std::size_t;
[[nodiscard]] auto clamp_selection_cursor(std::size_t cursor, std::size_t match_count) -> std::size_t;
[[nodiscard]] auto selected_match_index(std::span<const std::size_t> matches, std::size_t cursor)
    -> std::optional<std::size_t>;
[[nodiscard]] auto visible_selection_count(std::size_t match_count, std::size_t page_size) -> std::size_t;
[[nodiscard]] auto hidden_selection_count(std::size_t match_count, std::size_t page_size) -> std::size_t;

} // namespace codeharness::tui

#include "codeharness/tui/selection_list.h"

#include <algorithm>

namespace codeharness::tui
{

auto move_selection_up(std::size_t cursor, std::size_t match_count) -> std::size_t
{
    if (match_count == 0)
    {
        return cursor;
    }

    cursor = clamp_selection_cursor(cursor, match_count);
    return cursor == 0 ? match_count - 1 : cursor - 1;
}

auto move_selection_down(std::size_t cursor, std::size_t match_count) -> std::size_t
{
    if (match_count == 0)
    {
        return cursor;
    }

    cursor = clamp_selection_cursor(cursor, match_count);
    return (cursor + 1) % match_count;
}

auto clamp_selection_cursor(std::size_t cursor, std::size_t match_count) -> std::size_t
{
    if (cursor >= match_count)
    {
        return 0;
    }
    return cursor;
}

auto selected_match_index(std::span<const std::size_t> matches, std::size_t cursor) -> std::optional<std::size_t>
{
    if (cursor >= matches.size())
    {
        return std::nullopt;
    }
    return matches[cursor];
}

auto visible_selection_count(std::size_t match_count, std::size_t page_size) -> std::size_t
{
    return std::min(match_count, page_size);
}

auto hidden_selection_count(std::size_t match_count, std::size_t page_size) -> std::size_t
{
    if (match_count <= page_size)
    {
        return 0;
    }
    return match_count - page_size;
}

} // namespace codeharness::tui

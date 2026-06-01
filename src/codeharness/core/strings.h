#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

namespace codeharness
{

inline auto trim(std::string_view value) -> std::string_view
{
    constexpr auto whitespace = " \t\n\r\f\v";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

// 从 offset 起读一行，并返回下一行的起始 offset
inline auto next_line(std::string_view text, std::size_t offset)
    -> std::pair<std::string_view, std::size_t>
{
    const auto newline = text.find('\n', offset);
    if (newline == std::string_view::npos)
    {
        return {text.substr(offset), text.size()};
    }
    return {text.substr(offset, newline - offset), newline + 1};
}

} // namespace codeharness

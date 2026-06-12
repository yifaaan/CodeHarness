#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace codeharness
{

inline auto LowerAscii(char character) -> char
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

inline auto Slugify(std::string_view text) -> std::string
{
    std::string slug;
    bool previous_separator = true;

    for (const auto character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0)
        {
            slug.push_back(LowerAscii(character));
            previous_separator = false;
            continue;
        }

        if (!previous_separator)
        {
            slug.push_back('_');
            previous_separator = true;
        }
    }

    while (!slug.empty() && slug.back() == '_')
    {
        slug.pop_back();
    }

    if (slug.empty())
    {
        return "memory";
    }

    return slug;
}

inline auto Trim(std::string_view value) -> std::string_view
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

inline auto StripTrailingCr(std::string_view value) -> std::string_view
{
    if (!value.empty() && value.back() == '\r')
    {
        return value.substr(0, value.size() - 1);
    }
    return value;
}

// 从 offset 起读一行，并返回下一行的起始 offset
inline auto NextLine(std::string_view text, std::size_t offset)
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

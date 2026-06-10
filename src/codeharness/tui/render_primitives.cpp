#include "codeharness/tui/render_primitives.h"

#include <algorithm>

namespace codeharness::tui::render
{

auto trim_to_width(std::string text, int width) -> std::string
{
    if (width > 0 && static_cast<int>(text.size()) > width)
    {
        text.resize(static_cast<std::size_t>(width));
    }
    return text;
}

auto horizontal_rule(int width) -> std::string
{
    const auto rule_width = std::max(width, 20);
    std::string rule;
    rule.reserve(static_cast<std::size_t>(rule_width) * 3);
    for (int index = 0; index < rule_width; ++index)
    {
        rule += "\xe2\x94\x80";
    }
    return rule;
}

} // namespace codeharness::tui::render

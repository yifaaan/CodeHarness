#include "codeharness/tui/render_primitives.h"

#include "codeharness/tui/tui_theme.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace codeharness::tui::render
{
namespace
{

[[nodiscard]] auto is_space(char c) noexcept -> bool
{
    return c == ' ' || c == '\t';
}

[[nodiscard]] auto looks_like_path(std::string_view token) noexcept -> bool
{
    return token.find('\\') != std::string_view::npos ||
           token.find('/') != std::string_view::npos ||
           token.find(".") != std::string_view::npos;
}

[[nodiscard]] auto is_flag(std::string_view token) noexcept -> bool
{
    return token.starts_with("-");
}

[[nodiscard]] auto role_for_command_token(std::string_view token, bool is_command) noexcept -> StyledColorRole
{
    if (is_command)
    {
        return StyledColorRole::command;
    }
    if (is_flag(token))
    {
        return StyledColorRole::argument;
    }
    if (looks_like_path(token))
    {
        return StyledColorRole::path;
    }
    return StyledColorRole::argument;
}

[[nodiscard]] auto shimmer_intensity(int char_index, int frame, int char_count) noexcept -> double
{
    constexpr auto padding = 10.0;
    constexpr auto band_half_width = 5.0;
    constexpr auto pi = 3.14159265358979323846;
    const auto period = static_cast<double>(char_count) + padding * 2.0;
    const auto position = std::fmod(static_cast<double>(std::max(frame, 0)) * 2.0, period) - padding;
    const auto distance = std::abs(static_cast<double>(char_index) - position);
    if (distance >= band_half_width)
    {
        return 0.0;
    }
    return 0.5 * (1.0 + std::cos(pi * distance / band_half_width));
}

} // namespace

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

auto plain_text(const StyledLine& line) -> std::string
{
    std::string result;
    for (const auto& segment : line)
    {
        result += segment.text;
    }
    return result;
}

auto color_for_role(StyledColorRole role) -> ftxui::Color
{
    switch (role)
    {
        case StyledColorRole::foreground:
            return TuiTheme::text_default();
        case StyledColorRole::strong:
            return TuiTheme::text_strong();
        case StyledColorRole::dim:
            return TuiTheme::text_dim();
        case StyledColorRole::muted:
            return TuiTheme::text_muted();
        case StyledColorRole::accent:
            return TuiTheme::accent();
        case StyledColorRole::success:
            return TuiTheme::success();
        case StyledColorRole::warning:
            return TuiTheme::warning();
        case StyledColorRole::error:
            return TuiTheme::error();
        case StyledColorRole::command:
            return TuiTheme::command_name();
        case StyledColorRole::argument:
            return TuiTheme::command_argument();
        case StyledColorRole::path:
            return TuiTheme::command_path();
        case StyledColorRole::separator:
            return TuiTheme::command_separator();
        case StyledColorRole::output:
            return TuiTheme::codex_output_dim();
        case StyledColorRole::user_prefix:
            return TuiTheme::codex_user_prefix();
        case StyledColorRole::shimmer_base:
            return TuiTheme::shimmer_base();
        case StyledColorRole::shimmer_highlight:
            return TuiTheme::shimmer_highlight();
    }
    return TuiTheme::text_default();
}

auto styled_line_element(const StyledLine& line) -> ftxui::Element
{
    ftxui::Elements elements;
    elements.reserve(line.size());
    for (const auto& segment : line)
    {
        auto element = ftxui::text(segment.text);
        if (segment.foreground_rgb)
        {
            element = element | ftxui::color(to_ftxui_color(*segment.foreground_rgb));
        }
        else
        {
            element = element | ftxui::color(color_for_role(segment.role));
        }
        if (segment.bold)
        {
            element = element | ftxui::bold;
        }
        if (segment.dim)
        {
            element = element | ftxui::dim;
        }
        elements.push_back(element);
    }
    return ftxui::hbox(std::move(elements));
}

auto codex_command_header_segments(std::string_view line) -> StyledLine
{
    StyledLine segments;
    std::size_t index = 0;
    bool saw_status = false;
    bool saw_command = false;

    while (index < line.size())
    {
        const auto start = index;
        if (is_space(line[index]))
        {
            while (index < line.size() && is_space(line[index]))
            {
                ++index;
            }
            segments.push_back(StyledSegment{
                .text = std::string{line.substr(start, index - start)},
                .role = StyledColorRole::separator,
                .dim = true,
            });
            continue;
        }

        while (index < line.size() && !is_space(line[index]))
        {
            ++index;
        }

        const auto token = line.substr(start, index - start);
        if (!saw_status && (token == "Running" || token == "Ran" || token == "Failed"))
        {
            saw_status = true;
            segments.push_back(StyledSegment{
                .text = std::string{token},
                .role = StyledColorRole::muted,
                .dim = true,
            });
            continue;
        }

        const auto is_command = !saw_command;
        const auto role = role_for_command_token(token, is_command);
        segments.push_back(StyledSegment{
            .text = std::string{token},
            .role = role,
            .bold = is_command,
            .dim = !is_command && role == StyledColorRole::argument,
        });
        saw_command = true;
    }

    return segments;
}

auto shimmer_text_segments(std::string_view text, int frame, bool enabled, ShimmerColorMode mode) -> StyledLine
{
    if (!enabled || text.empty())
    {
        return StyledLine{StyledSegment{
            .text = std::string{text},
            .role = StyledColorRole::foreground,
        }};
    }

    StyledLine segments;
    segments.reserve(text.size());
    const auto char_count = static_cast<int>(text.size());
    const auto base = TuiTheme::shimmer_base_rgb();
    const auto highlight = TuiTheme::shimmer_highlight_rgb();

    for (int index = 0; index < char_count; ++index)
    {
        const auto intensity = shimmer_intensity(index, frame, char_count);
        auto segment = StyledSegment{
            .text = std::string{text.substr(static_cast<std::size_t>(index), 1)},
            .role = intensity >= 0.60 ? StyledColorRole::shimmer_highlight : StyledColorRole::shimmer_base,
            .bold = intensity >= 0.60,
            .dim = intensity < 0.20,
        };

        if (mode == ShimmerColorMode::TrueColor)
        {
            segment.foreground_rgb = blend(highlight, base, static_cast<float>(intensity * 0.90));
            segment.dim = false;
            segment.bold = intensity >= 0.35;
        }
        else if (intensity >= 0.20 && intensity < 0.60)
        {
            segment.role = StyledColorRole::foreground;
            segment.dim = false;
        }

        segments.push_back(std::move(segment));
    }
    return segments;
}

auto shimmer_text_element(std::string_view text, int frame, bool enabled) -> ftxui::Element
{
    return styled_line_element(shimmer_text_segments(text, frame, enabled));
}

auto brightest_shimmer_index(const StyledLine& line) -> std::optional<std::size_t>
{
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        if (line.at(index).bold || line.at(index).role == StyledColorRole::shimmer_highlight)
        {
            return index;
        }
    }
    return std::nullopt;
}

auto prefix_lines(const std::vector<std::string>& lines,
                  const std::string& initial_prefix,
                  const std::string& subsequent_prefix) -> std::vector<std::string>
{
    if (lines.empty())
    {
        return {};
    }

    std::vector<std::string> result;
    result.reserve(lines.size());
    result.push_back(initial_prefix + lines.front());
    for (std::size_t i = 1; i < lines.size(); ++i)
    {
        result.push_back(subsequent_prefix + lines.at(i));
    }
    return result;
}

} // namespace codeharness::tui::render

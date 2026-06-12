#pragma once

#include "codeharness/tui/color_utils.h"

#include <ftxui/dom/elements.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui::render
{

enum class StyledColorRole
{
    foreground,
    strong,
    dim,
    muted,
    accent,
    success,
    warning,
    error,
    command,
    argument,
    path,
    separator,
    output,
    user_prefix,
    shimmer_base,
    shimmer_highlight,
};

struct StyledSegment
{
    std::string text;
    StyledColorRole role = StyledColorRole::foreground;
    bool bold = false;
    bool dim = false;
    std::optional<RgbColor> foreground_rgb;
};

using StyledLine = std::vector<StyledSegment>;

enum class ShimmerColorMode
{
    TrueColor,
    Fallback,
};

[[nodiscard]] auto trim_to_width(std::string text, int width) -> std::string;
[[nodiscard]] auto horizontal_rule(int width) -> std::string;
[[nodiscard]] auto plain_text(const StyledLine& line) -> std::string;
[[nodiscard]] auto color_for_role(StyledColorRole role) -> ftxui::Color;
[[nodiscard]] auto styled_line_element(const StyledLine& line) -> ftxui::Element;
[[nodiscard]] auto codex_command_header_segments(std::string_view line) -> StyledLine;
[[nodiscard]] auto shimmer_text_segments(std::string_view text,
                                         int frame,
                                         bool enabled,
                                         ShimmerColorMode mode = ShimmerColorMode::TrueColor) -> StyledLine;
[[nodiscard]] auto shimmer_text_element(std::string_view text, int frame, bool enabled) -> ftxui::Element;
[[nodiscard]] auto brightest_shimmer_index(const StyledLine& line) -> std::optional<std::size_t>;

/// Prefix each line in `lines` with `initial_prefix` for the first line and `subsequent_prefix` for the rest.
[[nodiscard]] auto prefix_lines(const std::vector<std::string>& lines,
                                const std::string& initial_prefix,
                                const std::string& subsequent_prefix) -> std::vector<std::string>;

} // namespace codeharness::tui::render

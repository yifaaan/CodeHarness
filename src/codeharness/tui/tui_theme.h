#pragma once

#include "codeharness/tui/color_utils.h"

#include <ftxui/screen/color.hpp>
#include <optional>

namespace codeharness::tui
{

/// Terminal color level support detection.
enum class ColorLevel
{
    Ansi16,     ///< Basic 16-color ANSI palette
    Ansi256,    ///< Extended 256-color palette
    TrueColor,  ///< Full 24-bit true color support
    Unknown,    ///< Unable to detect color support
};

/// Terminal palette information detected at startup.
struct TerminalPalette
{
    std::optional<RgbColor> default_fg;
    std::optional<RgbColor> default_bg;
    ColorLevel color_level = ColorLevel::Unknown;
    bool is_dark_theme = true;  ///< True if terminal background is dark

    static auto detect() -> TerminalPalette;
};

/// Global terminal palette state (set at startup).
inline TerminalPalette g_terminal_palette;

/// Initialize the terminal palette by detecting terminal capabilities.
inline auto init_terminal_palette() -> void
{
    g_terminal_palette = TerminalPalette::detect();
}

/// Get the default foreground color (or nullopt if unknown).
[[nodiscard]] inline auto default_fg() noexcept -> std::optional<RgbColor>
{
    return g_terminal_palette.default_fg;
}

/// Get the default background color (or nullopt if unknown).
[[nodiscard]] inline auto default_bg() noexcept -> std::optional<RgbColor>
{
    return g_terminal_palette.default_bg;
}

/// Get the detected color level.
[[nodiscard]] inline auto stdout_color_level() noexcept -> ColorLevel
{
    return g_terminal_palette.color_level;
}

/// Check if terminal is using a dark theme.
[[nodiscard]] inline auto is_dark_theme() noexcept -> bool
{
    return g_terminal_palette.is_dark_theme;
}

/// Convert RGB color to ftxui::Color based on terminal capabilities.
[[nodiscard]] inline auto to_ftxui_color(RgbColor rgb) -> ftxui::Color
{
    const auto level = stdout_color_level();

    switch (level)
    {
        case ColorLevel::TrueColor:
            // ftxui uses Color::RGB(r, g, b) for true color
            return ftxui::Color{rgb.r, rgb.g, rgb.b};

        case ColorLevel::Ansi256:
        {
            const auto index = best_256_color(rgb);
            // ftxui uses Palette256 enum for 256 colors
            return ftxui::Color{static_cast<ftxui::Color::Palette256>(index)};
        }

        case ColorLevel::Ansi16:
        case ColorLevel::Unknown:
        default:
            // Fall back to basic ANSI colors using Palette16 enum
            if (is_light(rgb))
            {
                return ftxui::Color{ftxui::Color::White};
            }
            return ftxui::Color{ftxui::Color::GrayDark};
    }
}

/// Get the best ftxui::Color for an RGB value, considering terminal capabilities.
[[nodiscard]] inline auto best_color(RgbColor rgb) -> ftxui::Color
{
    return to_ftxui_color(rgb);
}

// ============================================================================
// Theme Colors - Codex-cli Style
// ============================================================================

/// Theme colors that adapt to terminal background.
struct TuiTheme
{
    // Primary colors (adapt to terminal background)
    [[nodiscard]] static auto primary() -> ftxui::Color
    {
        if (const auto bg = default_bg())
        {
            return to_ftxui_color(accent_color_for_bg(*bg));
        }
        return ftxui::Color::Cyan;
    }

    [[nodiscard]] static auto accent() -> ftxui::Color
    {
        return primary();
    }

    // Text colors
    [[nodiscard]] static auto text_strong() -> ftxui::Color
    {
        return ftxui::Color::White;
    }

    [[nodiscard]] static auto text_dim() -> ftxui::Color
    {
        return ftxui::Color::GrayDark;
    }

    [[nodiscard]] static auto text_muted() -> ftxui::Color
    {
        return ftxui::Color::GrayLight;
    }

    // Semantic colors
    [[nodiscard]] static auto success() -> ftxui::Color
    {
        return ftxui::Color::Green;
    }

    [[nodiscard]] static auto warning() -> ftxui::Color
    {
        return ftxui::Color::Yellow;
    }

    [[nodiscard]] static auto error() -> ftxui::Color
    {
        return ftxui::Color::Red;
    }

    // Background colors for content blocks
    [[nodiscard]] static auto user_message_bg() -> ftxui::Color
    {
        if (const auto bg = default_bg())
        {
            return to_ftxui_color(codeharness::tui::user_message_bg(*bg));
        }
        return ftxui::Color::Default;  // Use terminal default
    }

    [[nodiscard]] static auto proposed_plan_bg() -> ftxui::Color
    {
        return user_message_bg();
    }

    // Table separator (low contrast, decorative)
    [[nodiscard]] static auto table_separator() -> ftxui::Color
    {
        if (const auto fg = default_fg())
        {
            if (const auto bg = default_bg())
            {
                // Blend 20% of foreground with background for subtle separators
                const auto separator_rgb = blend(*fg, *bg, 0.20F);
                return to_ftxui_color(separator_rgb);
            }
        }
        return ftxui::Color::GrayDark;
    }

    // Status indicator colors
    [[nodiscard]] static auto status_working() -> ftxui::Color
    {
        return primary();
    }

    [[nodiscard]] static auto status_success() -> ftxui::Color
    {
        return success();
    }

    [[nodiscard]] static auto status_error() -> ftxui::Color
    {
        return error();
    }

    // Tool-specific colors
    [[nodiscard]] static auto tool_running() -> ftxui::Color
    {
        return ftxui::Color::Yellow;
    }

    [[nodiscard]] static auto tool_completed() -> ftxui::Color
    {
        return ftxui::Color::Green;
    }

    [[nodiscard]] static auto tool_failed() -> ftxui::Color
    {
        return ftxui::Color::Red;
    }

    // Border colors
    [[nodiscard]] static auto border_default() -> ftxui::Color
    {
        return ftxui::Color::GrayDark;
    }

    [[nodiscard]] static auto border_focused() -> ftxui::Color
    {
        return primary();
    }
};

// ============================================================================
// Unicode Symbols for UI
// ============================================================================

/// Select pointer symbol (❯)
inline constexpr std::string_view k_select_pointer = "\xe2\x9d\xaf ";

/// Current marker (← current)
inline constexpr std::string_view k_current_mark = " \xe2\x86\x90 current";

/// Box drawing characters
inline constexpr std::string_view k_box_horizontal = "\xe2\x94\x80";  // ─
inline constexpr std::string_view k_box_vertical = "\xe2\x94\x82";   // │
inline constexpr std::string_view k_box_corner_tl = "\xe2\x94\x8c";  // ┌
inline constexpr std::string_view k_box_corner_tr = "\xe2\x94\x90";  // ┐
inline constexpr std::string_view k_box_corner_bl = "\xe2\x94\x94";  // └
inline constexpr std::string_view k_box_corner_br = "\xe2\x94\x98";  // ┘
inline constexpr std::string_view k_box_tee_right = "\xe2\x94\x9c";  // ├
inline constexpr std::string_view k_box_tee_left = "\xe2\x94\xa4";   // ┤

/// Tree drawing characters
inline constexpr std::string_view k_tree_branch = "\xe2\x94\x9c ";   // ├
inline constexpr std::string_view k_tree_last = "\xe2\x94\x94 ";     // └

/// Spinner frames (Braille patterns for smooth animation)
inline constexpr std::string_view k_spinner_frames[] = {
    "\xe2\xa0\x8b",  // ⠋
    "\xe2\xa0\x99",  // ⠙
    "\xe2\xa0\xb9",  // ⠹
    "\xe2\xa0\xb8",  // ⠸
    "\xe2\xa0\xbc",  // ⠼
    "\xe2\xa0\xb4",  // ⠴
    "\xe2\xa0\xa6",  // ⠦
    "\xe2\xa0\xa7",  // ⠧
    "\xe2\xa0\x87",  // ⠇
    "\xe2\xa0\x8f",  // ⠏
};

/// Get spinner frame by index (cycling).
[[nodiscard]] inline auto spinner_frame(std::size_t index) noexcept -> std::string_view
{
    constexpr auto frame_count = sizeof(k_spinner_frames) / sizeof(k_spinner_frames[0]);
    return k_spinner_frames[index % frame_count];
}

/// Get spinner frame count.
[[nodiscard]] constexpr auto spinner_frame_count() noexcept -> std::size_t
{
    return sizeof(k_spinner_frames) / sizeof(k_spinner_frames[0]);
}

} // namespace codeharness::tui

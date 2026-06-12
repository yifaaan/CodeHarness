#pragma once

#include "codeharness/tui/color_utils.h"

#include <ftxui/screen/color.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace codeharness::tui
{

enum class ColorLevel
{
    Ansi16,
    Ansi256,
    TrueColor,
    Unknown,
};

struct TerminalPalette
{
    std::optional<RgbColor> default_fg;
    std::optional<RgbColor> default_bg;
    ColorLevel color_level = ColorLevel::Unknown;
    bool is_dark_theme = true;

    static auto detect() -> TerminalPalette;
};

inline TerminalPalette g_terminal_palette;

inline auto init_terminal_palette() -> void
{
    g_terminal_palette = TerminalPalette::detect();
}

[[nodiscard]] inline auto default_fg() noexcept -> std::optional<RgbColor>
{
    return g_terminal_palette.default_fg;
}

[[nodiscard]] inline auto default_bg() noexcept -> std::optional<RgbColor>
{
    return g_terminal_palette.default_bg;
}

[[nodiscard]] inline auto stdout_color_level() noexcept -> ColorLevel
{
    return g_terminal_palette.color_level;
}

[[nodiscard]] inline auto is_dark_theme() noexcept -> bool
{
    return g_terminal_palette.is_dark_theme;
}

[[nodiscard]] inline auto to_ftxui_color(RgbColor rgb) -> ftxui::Color
{
    switch (stdout_color_level())
    {
        case ColorLevel::TrueColor:
            return ftxui::Color{rgb.r, rgb.g, rgb.b};
        case ColorLevel::Ansi256:
            return ftxui::Color{static_cast<ftxui::Color::Palette256>(best_256_color(rgb))};
        case ColorLevel::Ansi16:
        case ColorLevel::Unknown:
        default:
            return is_light(rgb) ? ftxui::Color{ftxui::Color::White} : ftxui::Color{ftxui::Color::GrayDark};
    }
}

[[nodiscard]] inline auto best_color(RgbColor rgb) -> ftxui::Color
{
    return to_ftxui_color(rgb);
}

struct TuiTheme
{
    [[nodiscard]] static constexpr auto codex_background_rgb() noexcept -> RgbColor
    {
        return RgbColor{30, 30, 30};
    }

    [[nodiscard]] static constexpr auto codex_foreground_rgb() noexcept -> RgbColor
    {
        return RgbColor{212, 212, 212};
    }

    [[nodiscard]] static constexpr auto codex_strong_rgb() noexcept -> RgbColor
    {
        return RgbColor{245, 245, 245};
    }

    [[nodiscard]] static constexpr auto codex_dim_rgb() noexcept -> RgbColor
    {
        return RgbColor{111, 111, 111};
    }

    [[nodiscard]] static constexpr auto codex_muted_rgb() noexcept -> RgbColor
    {
        return RgbColor{138, 138, 138};
    }

    [[nodiscard]] static constexpr auto codex_separator_rgb() noexcept -> RgbColor
    {
        return RgbColor{106, 106, 106};
    }

    [[nodiscard]] static constexpr auto codex_accent_rgb() noexcept -> RgbColor
    {
        return RgbColor{0, 200, 232};
    }

    [[nodiscard]] static constexpr auto codex_success_rgb() noexcept -> RgbColor
    {
        return RgbColor{25, 195, 125};
    }

    [[nodiscard]] static constexpr auto codex_warning_rgb() noexcept -> RgbColor
    {
        return RgbColor{255, 214, 102};
    }

    [[nodiscard]] static constexpr auto codex_error_rgb() noexcept -> RgbColor
    {
        return RgbColor{255, 92, 92};
    }

    [[nodiscard]] static constexpr auto codex_command_rgb() noexcept -> RgbColor
    {
        return RgbColor{138, 174, 255};
    }

    [[nodiscard]] static constexpr auto codex_argument_rgb() noexcept -> RgbColor
    {
        return RgbColor{245, 158, 190};
    }

    [[nodiscard]] static constexpr auto codex_path_rgb() noexcept -> RgbColor
    {
        return RgbColor{205, 170, 255};
    }

    [[nodiscard]] static constexpr auto codex_output_rgb() noexcept -> RgbColor
    {
        return RgbColor{126, 126, 126};
    }

    [[nodiscard]] static auto background() -> ftxui::Color
    {
        if (stdout_color_level() == ColorLevel::Ansi16 || stdout_color_level() == ColorLevel::Unknown)
        {
            return ftxui::Color::Black;
        }
        return to_ftxui_color(codex_background_rgb());
    }

    [[nodiscard]] static auto foreground() -> ftxui::Color
    {
        return to_ftxui_color(codex_foreground_rgb());
    }

    [[nodiscard]] static auto primary() -> ftxui::Color
    {
        return to_ftxui_color(codex_accent_rgb());
    }

    [[nodiscard]] static auto accent() -> ftxui::Color
    {
        return primary();
    }

    [[nodiscard]] static auto text_strong() -> ftxui::Color
    {
        return to_ftxui_color(codex_strong_rgb());
    }

    [[nodiscard]] static auto text_default() -> ftxui::Color
    {
        return foreground();
    }

    [[nodiscard]] static auto text_dim() -> ftxui::Color
    {
        return to_ftxui_color(codex_dim_rgb());
    }

    [[nodiscard]] static auto text_muted() -> ftxui::Color
    {
        return to_ftxui_color(codex_muted_rgb());
    }

    [[nodiscard]] static auto success() -> ftxui::Color
    {
        return to_ftxui_color(codex_success_rgb());
    }

    [[nodiscard]] static auto warning() -> ftxui::Color
    {
        return to_ftxui_color(codex_warning_rgb());
    }

    [[nodiscard]] static auto error() -> ftxui::Color
    {
        return to_ftxui_color(codex_error_rgb());
    }

    [[nodiscard]] static auto user_message_bg() -> ftxui::Color
    {
        return to_ftxui_color(codeharness::tui::user_message_bg(codex_background_rgb()));
    }

    [[nodiscard]] static auto proposed_plan_bg() -> ftxui::Color
    {
        return user_message_bg();
    }

    [[nodiscard]] static auto table_separator() -> ftxui::Color
    {
        return to_ftxui_color(codex_separator_rgb());
    }

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

    [[nodiscard]] static auto tool_running() -> ftxui::Color
    {
        return primary();
    }

    [[nodiscard]] static auto tool_completed() -> ftxui::Color
    {
        return codex_bullet();
    }

    [[nodiscard]] static auto tool_failed() -> ftxui::Color
    {
        return error();
    }

    [[nodiscard]] static auto codex_bullet() -> ftxui::Color
    {
        return to_ftxui_color(blend(codex_foreground_rgb(), codex_background_rgb(), 0.45F));
    }

    [[nodiscard]] static auto codex_user_prefix() -> ftxui::Color
    {
        return to_ftxui_color(blend(codex_foreground_rgb(), codex_background_rgb(), 0.60F));
    }

    [[nodiscard]] static auto codex_output_dim() -> ftxui::Color
    {
        return to_ftxui_color(codex_output_rgb());
    }

    [[nodiscard]] static auto border_default() -> ftxui::Color
    {
        return to_ftxui_color(blend(codex_foreground_rgb(), codex_background_rgb(), 0.22F));
    }

    [[nodiscard]] static auto border_focused() -> ftxui::Color
    {
        return primary();
    }

    [[nodiscard]] static auto command_name() -> ftxui::Color
    {
        return to_ftxui_color(codex_command_rgb());
    }

    [[nodiscard]] static auto command_argument() -> ftxui::Color
    {
        return to_ftxui_color(codex_argument_rgb());
    }

    [[nodiscard]] static auto command_path() -> ftxui::Color
    {
        return to_ftxui_color(codex_path_rgb());
    }

    [[nodiscard]] static auto command_separator() -> ftxui::Color
    {
        return text_dim();
    }

    [[nodiscard]] static auto shimmer_base_rgb() noexcept -> RgbColor
    {
        return codex_dim_rgb();
    }

    [[nodiscard]] static auto shimmer_highlight_rgb() noexcept -> RgbColor
    {
        return RgbColor{245, 245, 245};
    }

    [[nodiscard]] static auto shimmer_base() -> ftxui::Color
    {
        return to_ftxui_color(shimmer_base_rgb());
    }

    [[nodiscard]] static auto shimmer_highlight() -> ftxui::Color
    {
        return to_ftxui_color(RgbColor{245, 245, 245});
    }
};

inline constexpr std::string_view k_select_pointer = "\xe2\x9d\xaf ";
inline constexpr std::string_view k_current_mark = " \xe2\x86\x90 current";
inline constexpr std::string_view k_codex_prompt_prefix = "\xe2\x80\xba ";
inline constexpr std::string_view k_codex_bullet = "\xe2\x80\xa2 ";

inline constexpr std::string_view k_box_horizontal = "\xe2\x94\x80";
inline constexpr std::string_view k_box_vertical = "\xe2\x94\x82";
inline constexpr std::string_view k_box_corner_tl = "\xe2\x94\x8c";
inline constexpr std::string_view k_box_corner_tr = "\xe2\x94\x90";
inline constexpr std::string_view k_box_corner_bl = "\xe2\x94\x94";
inline constexpr std::string_view k_box_corner_br = "\xe2\x94\x98";

// Rounded corners (Codex-style session header card)
inline constexpr std::string_view k_round_corner_tl = "\xe2\x95\xad";
inline constexpr std::string_view k_round_corner_tr = "\xe2\x95\xae";
inline constexpr std::string_view k_round_corner_bl = "\xe2\x95\xb0";
inline constexpr std::string_view k_round_corner_br = "\xe2\x95\xaf";
inline constexpr std::string_view k_box_tee_right = "\xe2\x94\x9c";
inline constexpr std::string_view k_box_tee_left = "\xe2\x94\xa4";
inline constexpr std::string_view k_tree_branch = "\xe2\x94\x82 ";
inline constexpr std::string_view k_tree_last = "\xe2\x94\x94 ";

inline constexpr std::string_view k_spinner_frames[] = {
    "\xe2\xa0\x8b",
    "\xe2\xa0\x99",
    "\xe2\xa0\xb9",
    "\xe2\xa0\xb8",
    "\xe2\xa0\xbc",
    "\xe2\xa0\xb4",
    "\xe2\xa0\xa6",
    "\xe2\xa0\xa7",
    "\xe2\xa0\x87",
    "\xe2\xa0\x8f",
};

[[nodiscard]] inline auto spinner_frame(std::size_t index) noexcept -> std::string_view
{
    constexpr auto frame_count = sizeof(k_spinner_frames) / sizeof(k_spinner_frames[0]);
    return k_spinner_frames[index % frame_count];
}

[[nodiscard]] constexpr auto spinner_frame_count() noexcept -> std::size_t
{
    return sizeof(k_spinner_frames) / sizeof(k_spinner_frames[0]);
}

} // namespace codeharness::tui

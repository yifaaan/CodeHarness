#pragma once

#include "codeharness/tui/color_utils.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>
#include <optional>
#include <string>

namespace codeharness::tui
{

// ============================================================================
// Dynamic Style Utilities (codex-cli style.rs port)
// ============================================================================

/// Style for user-authored messages, adapting to terminal background.
/// On color-capable terminals: adds a subtle background tint.
/// On basic terminals: uses default styling.
[[nodiscard]] inline auto user_message_bg_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::bgcolor(TuiTheme::user_message_bg());
    };
}

/// Style for proposed plan cells.
[[nodiscard]] inline auto proposed_plan_bg_style() -> ftxui::Decorator
{
    return user_message_bg_style();
}

/// Fill the top-level TUI with the Codex dark background when the terminal supports it.
[[nodiscard]] inline auto codex_background_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return ftxui::dbox({
            ftxui::filler() | ftxui::bgcolor(TuiTheme::background()),
            child | ftxui::bgcolor(TuiTheme::background()),
        });
    };
}

/// Accent style for active or selected TUI controls.
/// Uses a bold accent color (cyan or darker variant on light backgrounds).
[[nodiscard]] inline auto accent_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::accent()) | ftxui::bold;
    };
}

/// Style for table separators (low contrast, decorative).
[[nodiscard]] inline auto table_separator_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::table_separator());
    };
}

/// Style for dimmed/de-emphasized text.
[[nodiscard]] inline auto dim_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::text_dim()) | ftxui::dim;
    };
}

/// Style for strong/prominent text.
[[nodiscard]] inline auto strong_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::bold | ftxui::color(TuiTheme::text_strong());
    };
}

/// Style for muted/subtle text (like hints).
[[nodiscard]] inline auto muted_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::text_muted()) | ftxui::dim;
    };
}

/// Style for success messages.
[[nodiscard]] inline auto success_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::success());
    };
}

/// Style for warning messages.
[[nodiscard]] inline auto warning_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::warning());
    };
}

/// Style for error messages.
[[nodiscard]] inline auto error_style() -> ftxui::Decorator
{
    return [](ftxui::Element child) -> ftxui::Element {
        return child | ftxui::color(TuiTheme::error());
    };
}

// ============================================================================
// Styled Element Builders
// ============================================================================

/// Create a styled header/title element.
[[nodiscard]] inline auto styled_header(const std::string& text) -> ftxui::Element
{
    return ftxui::hbox({
        ftxui::text(text) | accent_style(),
    });
}

/// Create a styled label (for key hints, etc.).
[[nodiscard]] inline auto styled_label(const std::string& key, const std::string& description) -> ftxui::Element
{
    return ftxui::hbox({
        ftxui::text(key) | ftxui::color(TuiTheme::primary()),
        ftxui::text(" " + description) | muted_style(),
    });
}

/// Create a styled separator line.
[[nodiscard]] inline auto styled_separator(int width) -> ftxui::Element
{
    std::string sep;
    sep.reserve(static_cast<std::size_t>(width) * 3);
    for (int i = 0; i < width; ++i)
    {
        sep += k_box_horizontal;
    }
    return ftxui::text(sep) | table_separator_style();
}

/// Create a styled bullet point.
[[nodiscard]] inline auto styled_bullet(const std::string& text, bool is_error = false) -> ftxui::Element
{
    auto bullet = ftxui::text(std::string{k_codex_bullet});
    if (is_error)
    {
        bullet = bullet | ftxui::color(TuiTheme::error());
    }
    else
    {
        bullet = bullet | muted_style();
    }

    auto content = ftxui::text(text);
    if (is_error)
    {
        content = content | ftxui::color(TuiTheme::error());
    }

    return ftxui::hbox({bullet, content});
}

/// Create a styled key binding hint (e.g., "Ctrl+C exit").
[[nodiscard]] inline auto styled_key_hint(const std::string& key, const std::string& action) -> ftxui::Element
{
    return ftxui::hbox({
        ftxui::text(key) | accent_style(),
        ftxui::text(" " + action) | muted_style(),
    });
}

/// Create a styled status line with working indicator.
[[nodiscard]] inline auto styled_status_working(const std::string& header, int elapsed_seconds) -> ftxui::Element
{
    // Format elapsed time
    std::string elapsed_str;
    if (elapsed_seconds < 60)
    {
        elapsed_str = std::to_string(elapsed_seconds) + "s";
    }
    else if (elapsed_seconds < 3600)
    {
        const auto mins = elapsed_seconds / 60;
        const auto secs = elapsed_seconds % 60;
        elapsed_str = std::to_string(mins) + "m " + (secs < 10 ? "0" : "") + std::to_string(secs) + "s";
    }
    else
    {
        const auto hours = elapsed_seconds / 3600;
        const auto mins = (elapsed_seconds % 3600) / 60;
        const auto secs = elapsed_seconds % 60;
        elapsed_str = std::to_string(hours) + "h " +
                      (mins < 10 ? "0" : "") + std::to_string(mins) + "m " +
                      (secs < 10 ? "0" : "") + std::to_string(secs) + "s";
    }

    return ftxui::hbox({
        ftxui::text(std::string{k_codex_bullet}) | ftxui::color(TuiTheme::codex_bullet()),
        ftxui::text(header) | ftxui::color(TuiTheme::text_strong()) | ftxui::bold,
        ftxui::text(" (" + elapsed_str + " \xe2\x80\xa2 esc to interrupt)") | muted_style(),
    });
}

/// Create a styled tool status line.
[[nodiscard]] inline auto styled_tool_status(const std::string& label, bool running, bool failed) -> ftxui::Element
{
    std::string status_text;
    ftxui::Color status_color;

    if (running)
    {
        status_text = "Running " + label;
        status_color = TuiTheme::tool_running();
    }
    else if (failed)
    {
        status_text = label + " failed";
        status_color = TuiTheme::tool_failed();
    }
    else
    {
        status_text = "Ran " + label;
        status_color = TuiTheme::tool_completed();
    }

    return ftxui::hbox({
        ftxui::text("\xe2\x80\xa2 ") | ftxui::color(status_color),
        ftxui::text(status_text) | ftxui::color(status_color),
    });
}

/// Create a styled permission prompt.
[[nodiscard]] inline auto styled_permission_prompt(
    const std::string& tool_name,
    const std::string& reason) -> ftxui::Element
{
    using namespace ftxui;

    Elements rows;

    // Header
    rows.push_back(hbox({
        text(std::string{k_box_corner_tl}) | warning_style(),
        text(" Allow ") | warning_style() | bold,
        text(tool_name) | color(TuiTheme::primary()) | bold,
        text("?") | bold,
    }));

    // Reason
    if (!reason.empty())
    {
        rows.push_back(hbox({
            text(std::string{k_box_vertical}) | warning_style(),
            text(" "),
            text(reason) | dim,
        }));
    }

    // Footer with actions
    rows.push_back(hbox({
        text(std::string{k_box_corner_bl}) | warning_style(),
        text(" "),
        text("[y] Allow once") | color(TuiTheme::success()),
        text("  "),
        text("[a] Allow session") | color(TuiTheme::success()),
        text("  "),
        text("[n/d] Deny") | color(TuiTheme::error()),
        text("  "),
        text("Esc cancel") | dim,
    }));

    return vbox(std::move(rows));
}

// ============================================================================
// Layout Helpers
// ============================================================================

/// Create a bordered box with title (simplified version).
[[nodiscard]] inline auto bordered_box(const std::string& title, ftxui::Element content) -> ftxui::Element
{
    using namespace ftxui;

    return vbox({
        // Top border with title
        hbox({
            text(std::string{k_box_corner_tl}),
            text(std::string{k_box_horizontal}),
            text(" "),
            text(title) | accent_style(),
            text(" "),
            filler(),
        }) | color(TuiTheme::border_default()),
        // Content row
        hbox({
            text(std::string{k_box_vertical}) | color(TuiTheme::border_default()),
            text(" "),
            content,
            filler(),
        }),
        // Bottom border
        hbox({
            text(std::string{k_box_corner_bl}) | color(TuiTheme::border_default()),
            filler() | color(TuiTheme::border_default()),
            text(std::string{k_box_corner_br}) | color(TuiTheme::border_default()),
        }) | color(TuiTheme::border_default()),
    });
}

/// Create a separator with text in the middle.
[[nodiscard]] inline auto text_separator(const std::string& sep_text, int width) -> ftxui::Element
{
    using namespace ftxui;

    const auto text_len = static_cast<int>(sep_text.size());
    const auto total_sep = width - text_len - 2;
    const auto left_width = total_sep / 2;
    const auto right_width = total_sep - left_width;

    std::string left_sep;
    std::string right_sep;
    for (int i = 0; i < left_width; ++i)
    {
        left_sep += k_box_horizontal;
    }
    for (int i = 0; i < right_width; ++i)
    {
        right_sep += k_box_horizontal;
    }

    return hbox({
        text(left_sep) | table_separator_style(),
        text(" " + sep_text + " ") | muted_style(),
        text(right_sep) | table_separator_style(),
    });
}

} // namespace codeharness::tui

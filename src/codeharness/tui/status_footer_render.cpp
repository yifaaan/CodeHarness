#include "codeharness/tui/status_footer_render.h"

#include "codeharness/permissions/permission.h"
#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_theme.h"

#include <sstream>
#include <string>

namespace codeharness::tui::render
{

using namespace ftxui;

auto format_token_count(int count) -> std::string
{
    if (count >= 1000)
    {
        auto whole = count / 1000;
        auto tenths = (count % 1000) / 100;
        return std::to_string(whole) + "." + std::to_string(tenths) + "k";
    }
    return std::to_string(count);
}

auto render_status_footer_line(const TuiDisplayConfig& config, const TuiState& state) -> std::string
{
    if (!config.status_line_items.empty())
    {
        std::ostringstream custom;
        for (std::size_t index = 0; index < config.status_line_items.size(); ++index)
        {
            if (index > 0)
            {
                custom << " \xc2\xb7 ";
            }
            custom << config.status_line_items.at(index);
        }
        return custom.str();
    }

    std::ostringstream output;
    output << config.model;
    if (!config.directory.empty())
    {
        output << " \xc2\xb7 " << config.directory;
    }
    if (state.permission_mode == PermissionMode::Plan)
    {
        output << " \xc2\xb7 Plan mode";
    }
    else if (state.permission_mode != PermissionMode::Default)
    {
        output << " \xc2\xb7 " << codeharness::permission_mode_label(state.permission_mode);
    }
    if (state.active_session)
    {
        output << " \xc2\xb7 " << state.active_session->session_id;
    }
    return output.str();
}

auto render_composer_hint(bool busy, int history_index) -> std::string
{
    if (busy)
    {
        return "esc stop \xc2\xb7 ctrl+c stop";
    }
    auto hint = std::string{"? for shortcuts"};
    if (history_index >= 0)
    {
        hint += " \xc2\xb7 history " + std::to_string(history_index + 1);
    }
    return hint;
}

auto busy_spinner_frame(int frame) -> std::string
{
    return std::string{spinner_frame(static_cast<std::size_t>(frame))};
}

auto should_animate_working_status(const TuiState& state) -> bool
{
    return state.busy && !state.interrupt_requested;
}

auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> Element
{
    Elements parts;

    if (!config.status_line_items.empty())
    {
        for (std::size_t index = 0; index < config.status_line_items.size(); ++index)
        {
            if (index > 0)
            {
                parts.push_back(text(" \xc2\xb7 ") | dim);
            }
            parts.push_back(text(config.status_line_items.at(index)) | dim);
        }
        return hbox(std::move(parts));
    }

    parts.push_back(text(config.model) | color(TuiTheme::primary()) | bold);
    if (!config.directory.empty())
    {
        parts.push_back(text(" \xc2\xb7 ") | dim);
        parts.push_back(text(config.directory) | color(TuiTheme::success()));
    }

    if (state.permission_mode == PermissionMode::Plan)
    {
        parts.push_back(text(" \xc2\xb7 ") | dim);
        parts.push_back(text("Plan mode") | color(TuiTheme::warning()) | bold);
    }
    else if (state.permission_mode != PermissionMode::Default)
    {
        parts.push_back(text(" \xc2\xb7 ") | dim);
        parts.push_back(text(std::string{codeharness::permission_mode_label(state.permission_mode)}) | dim);
    }
    if (state.active_session)
    {
        parts.push_back(text(" \xc2\xb7 ") | dim);
        parts.push_back(text(state.active_session->session_id) | dim);
    }

    return hbox(std::move(parts));
}

/// Format elapsed seconds into compact human-friendly form.
/// Matches codex-cli fmt_elapsed_compact: 0s, 59s, 1m 00s, 59m 59s, 1h 00m 00s
auto fmt_elapsed_compact(int elapsed_seconds) -> std::string
{
    if (elapsed_seconds < 60)
    {
        return std::to_string(elapsed_seconds) + "s";
    }
    if (elapsed_seconds < 3600)
    {
        const auto minutes = elapsed_seconds / 60;
        const auto seconds = elapsed_seconds % 60;
        return std::to_string(minutes) + "m " + (seconds < 10 ? "0" : "") + std::to_string(seconds) + "s";
    }
    const auto hours = elapsed_seconds / 3600;
    const auto minutes = (elapsed_seconds % 3600) / 60;
    const auto seconds = elapsed_seconds % 60;
    return std::to_string(hours) + "h " +
           (minutes < 10 ? "0" : "") + std::to_string(minutes) + "m " +
           (seconds < 10 ? "0" : "") + std::to_string(seconds) + "s";
}

/// Create a working status indicator with elapsed time and interrupt hint.
/// Matches codex-cli StatusIndicatorWidget styling.
auto working_status_element(int elapsed_seconds, const std::string& header, int animation_frame) -> Element
{
    auto elapsed_str = fmt_elapsed_compact(elapsed_seconds);

    return hbox({
        text(std::string{k_codex_bullet}) | color(TuiTheme::codex_bullet()) | dim,
        shimmer_text_element(header, animation_frame, true),
        text(" "),
        text("(" + elapsed_str + " \xe2\x80\xa2 esc to interrupt)") | muted_style(),
    });
}

/// Create a complete status footer with working indicator when busy.
auto full_status_footer_element(const TuiDisplayConfig& config,
                                const TuiState& state,
                                int elapsed_seconds,
                                int animation_frame) -> Element
{
    Elements rows;

    // Working indicator when busy
    if (state.busy && !state.interrupt_requested)
    {
        rows.push_back(working_status_element(elapsed_seconds, "Working", animation_frame));
    }
    else if (state.interrupt_requested)
    {
        rows.push_back(hbox({
            text("Interrupting...") | color(TuiTheme::warning()) | bold,
        }));
    }

    // Status info line
    rows.push_back(status_footer_element(config, state));

    return vbox(std::move(rows));
}

} // namespace codeharness::tui::render

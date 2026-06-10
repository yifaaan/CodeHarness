#include "codeharness/tui/status_footer_render.h"

#include "codeharness/permissions/permission.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_theme.h"

#include <sstream>
#include <string_view>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

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

/// Build the status line parts for display.
auto build_status_parts(const TuiDisplayConfig& config, const TuiState& state) -> Elements
{
    Elements parts;

    // Model and provider
    parts.push_back(text("model: ") | color(TuiTheme::primary()) | dim);
    parts.push_back(text(config.model) | dim);
    parts.push_back(text(std::string{k_separator}) | dim);

    parts.push_back(text("provider: ") | dim);
    parts.push_back(text(config.provider_type) | dim);

    // Token usage (if available)
    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("tokens: ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.input_tokens) + "\xe2\x86\x93 ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.output_tokens) + "\xe2\x86\x91") | dim);
    }

    // Skills count
    if (config.skill_count > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("skills: " + std::to_string(config.skill_count)) | dim);
    }

    // Active session
    if (state.active_session)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("session: " + state.active_session->session_id) | dim);
    }

    // MCP connections
    if (config.mcp_info.connected > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        auto mcp_text = "mcp: " + std::to_string(config.mcp_info.connected);
        if (config.mcp_info.failed > 0)
        {
            mcp_text += "/" + std::to_string(config.mcp_info.failed);
        }
        parts.push_back(text(mcp_text) | dim);
    }

    // Permission mode (except Plan mode)
    if (state.permission_mode != PermissionMode::Plan)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("mode: " + std::string{codeharness::permission_mode_label(state.permission_mode)}) | dim);
    }

    // Plan mode indicator
    if (state.permission_mode == PermissionMode::Plan)
    {
        parts.push_back(text(" [PLAN MODE] ") | color(TuiTheme::warning()) | bold);
    }

    return parts;
}

} // namespace

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
    std::ostringstream output;
    output << "model: " << config.model << " \xe2\x94\x82 provider: " << config.provider_type;
    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        output << " \xe2\x94\x82 tokens: " << format_token_count(config.token_usage.input_tokens)
               << "\xe2\x86\x93 " << format_token_count(config.token_usage.output_tokens) << "\xe2\x86\x91";
    }
    if (config.skill_count > 0)
    {
        output << " \xe2\x94\x82 skills: " << config.skill_count;
    }
    if (state.active_session)
    {
        output << " \xe2\x94\x82 session: " << state.active_session->session_id;
    }
    if (config.mcp_info.connected > 0)
    {
        output << " \xe2\x94\x82 mcp: " << config.mcp_info.connected;
        if (config.mcp_info.failed > 0)
        {
            output << "/" << config.mcp_info.failed;
        }
    }
    if (state.permission_mode != PermissionMode::Plan)
    {
        output << " \xe2\x94\x82 mode: " << codeharness::permission_mode_label(state.permission_mode);
    }
    return output.str();
}

auto render_composer_hint(bool busy, int history_index) -> std::string
{
    if (busy)
    {
        return "Esc stop \xc2\xb7 Ctrl+C stop";
    }
    auto hint = std::string{"Shift+Enter newline \xc2\xb7 Enter send \xc2\xb7 / commands \xc2\xb7 Ctrl+P/N history \xc2\xb7 Ctrl+C exit"};
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

auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> Element
{
    return hbox(build_status_parts(config, state));
}

/// Create a working status indicator with elapsed time and interrupt hint.
/// Matches codex-cli StatusIndicatorWidget styling.
auto working_status_element(int elapsed_seconds, const std::string& header) -> Element
{
    auto elapsed_str = fmt_elapsed_compact(elapsed_seconds);

    return hbox({
        text(busy_spinner_frame(elapsed_seconds % spinner_frame_count())) | color(TuiTheme::primary()),
        text(" "),
        text(header) | accent_style(),
        text(" "),
        text("(" + elapsed_str + " \xe2\x80\xa2 Esc to interrupt)") | muted_style(),
    });
}

/// Create a complete status footer with working indicator when busy.
auto full_status_footer_element(const TuiDisplayConfig& config, const TuiState& state, int elapsed_seconds) -> Element
{
    using namespace ftxui;

    Elements rows;

    // Working indicator when busy
    if (state.busy && !state.interrupt_requested)
    {
        rows.push_back(working_status_element(elapsed_seconds, "Working"));
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
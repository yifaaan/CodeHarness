#include "codeharness/tui/status_footer_render.h"

#include "codeharness/permissions/permission.h"
#include "codeharness/tui/tui_theme.h"

#include <sstream>
#include <string_view>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

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
    static constexpr std::string_view frames[] = {
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
    return std::string{frames[static_cast<std::size_t>(frame) % (sizeof(frames) / sizeof(frames[0]))]};
}

auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> Element
{
    Elements parts;

    parts.push_back(text("model: ") | color(TuiTheme::primary()) | dim);
    parts.push_back(text(config.model) | dim);
    parts.push_back(text(std::string{k_separator}) | dim);

    parts.push_back(text("provider: ") | dim);
    parts.push_back(text(config.provider_type) | dim);

    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("tokens: ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.input_tokens) + "\xe2\x86\x93 ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.output_tokens) + "\xe2\x86\x91") | dim);
    }

    if (config.skill_count > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("skills: " + std::to_string(config.skill_count)) | dim);
    }

    if (state.active_session)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("session: " + state.active_session->session_id) | dim);
    }

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

    if (state.permission_mode != PermissionMode::Plan)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("mode: " + std::string{codeharness::permission_mode_label(state.permission_mode)}) | dim);
    }

    if (state.permission_mode == PermissionMode::Plan)
    {
        parts.push_back(text(" [PLAN MODE] ") | color(TuiTheme::warning()) | bold);
    }
    return hbox(std::move(parts));
}

} // namespace codeharness::tui::render

#include "codeharness/tui/tui_render.h"

#include "codeharness/tui/tui_markdown.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <sstream>
#include <string_view>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

auto trim_to_width(std::string text, int width) -> std::string
{
    if (width > 0 && static_cast<int>(text.size()) > width)
    {
        text.resize(static_cast<std::size_t>(width));
    }
    return text;
}

auto split_lines(std::string_view text) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::istringstream stream{std::string{text}};
    for (std::string line; std::getline(stream, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty())
        {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

auto tool_status_suffix(const TranscriptItem& item) -> std::string
{
    if (item.tool_status == ToolStatus::running)
    {
        return "";
    }
    if (item.is_error)
    {
        return " error";
    }
    const auto lines = tool_line_count(item.detail);
    if (lines > 0)
    {
        return " " + std::to_string(lines) + "L";
    }
    return "";
}

} // namespace

auto horizontal_rule(int width) -> std::string
{
    const auto rule_width = std::max(width, 20);
    return std::string(static_cast<std::size_t>(rule_width), '\xe2\x94\x80'); // ─
}

auto permission_mode_label(PermissionMode mode) -> std::string
{
    switch (mode)
    {
    case PermissionMode::Plan:
        return "plan";
    case PermissionMode::FullAuto:
        return "full_auto";
    default:
        return "default";
    }
}

auto format_token_count(int count) -> std::string
{
    if (count >= 1000)
    {
        // Format as "N.Nk"
        auto whole = count / 1000;
        auto tenths = (count % 1000) / 100;
        return std::to_string(whole) + "." + std::to_string(tenths) + "k";
    }
    return std::to_string(count);
}

auto tool_line_count(std::string_view detail) -> int
{
    return static_cast<int>(split_lines(detail).size());
}

auto tool_summary_text(const TranscriptItem& item) -> std::string
{
    std::ostringstream output;
    if (item.tool_status == ToolStatus::running)
    {
        output << "Running " << item.label;
    }
    else
    {
        output << "Ran " << item.label << tool_status_suffix(item);
    }
    return output.str();
}

auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>
{
    return {
        "CodeHarness",
        "An AI-powered coding assistant  v" + config.version,
        "",
        "/help commands  |  /skills list  |  Ctrl+C exit",
    };
}

auto render_transcript_lines(const std::vector<TranscriptItem>& transcript, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    for (const auto& item : transcript)
    {
        if (item.kind == "user")
        {
            for (const auto& line : split_lines(item.text))
            {
                lines.push_back("> " + trim_to_width(line, width > 2 ? width - 2 : width));
            }
            continue;
        }
        if (item.kind == "assistant")
        {
            const auto rendered = markdown::render_plain_text(item.text, width);
            for (const auto& line : split_lines(rendered))
            {
                lines.push_back(trim_to_width(line, width));
            }
            continue;
        }
        if (item.kind == "system")
        {
            lines.push_back("[system] " + trim_to_width(item.text, width > 9 ? width - 9 : width));
            continue;
        }
        if (item.kind == "tool")
        {
            lines.push_back("• " + trim_to_width(tool_summary_text(item), width > 2 ? width - 2 : width));
            if (item.expanded && !item.detail.empty())
            {
                if (item.is_error)
                {
                    const auto error_lines = split_lines(item.detail);
                    const auto visible = std::min(error_lines.size(), static_cast<std::size_t>(k_tool_error_max_lines));
                    for (std::size_t index = 0; index < visible; ++index)
                    {
                        const auto prefix = index + 1 == visible ? "└ " : "│ ";
                        lines.push_back(prefix + trim_to_width(error_lines.at(index), width > 2 ? width - 2 : width));
                    }
                    if (error_lines.size() > visible)
                    {
                        lines.push_back("└ ... (" + std::to_string(error_lines.size() - visible) + " more lines)");
                    }
                }
                else
                {
                    for (const auto& line : split_lines(item.detail))
                    {
                        lines.push_back(trim_to_width(line, width));
                    }
                }
            }
            continue;
        }
        if (item.kind == "error")
        {
            lines.push_back(trim_to_width(item.text, width));
        }
    }
    return lines;
}

auto render_command_palette_lines(const CommandPaletteState& palette, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back(horizontal_rule(width));
    auto title = std::string{"Select a command"};
    if (palette.query.empty())
    {
        title += "  (type to search)";
    }
    lines.push_back(title);
    lines.push_back(palette.query.empty() ? "↑↓ navigate · Enter select · Esc cancel"
                                        : "↑↓ navigate · Enter select · Esc cancel · Backspace clear");
    lines.push_back("");
    if (!palette.query.empty())
    {
        lines.push_back("Search: " + palette.query);
    }

    const auto visible = std::min(palette.matches.size(), static_cast<std::size_t>(k_command_palette_page_size));
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto command_index = palette.matches.at(row);
        const auto& command = palette.commands.at(command_index);
        const auto pointer = row == palette.cursor ? std::string{k_select_pointer} : "  ";
        lines.push_back(trim_to_width(pointer + "/" + command.name + "  " + command.description, width));
    }

    if (palette.matches.empty())
    {
        lines.push_back("No matches");
    }
    else if (palette.matches.size() > visible)
    {
        lines.push_back("▼ " + std::to_string(palette.matches.size() - visible) + " more");
    }

    lines.push_back("");
    lines.push_back(horizontal_rule(width));
    return lines;
}

auto render_permission_lines(const PermissionPrompt& prompt, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back("┌ Allow " + prompt.tool_name + "?");
    if (!prompt.reason.empty())
    {
        lines.push_back("│ " + trim_to_width(prompt.reason, width > 2 ? width - 2 : width));
    }
    if (prompt.command)
    {
        lines.push_back("│ command: " + trim_to_width(*prompt.command, width > 11 ? width - 11 : width));
    }
    if (prompt.path)
    {
        lines.push_back("│ path: " + trim_to_width(prompt.path->string(), width > 8 ? width - 8 : width));
    }
    lines.push_back("└ [y] Allow once  [a] Allow session  [n] Deny");
    return lines;
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
        output << " \xe2\x94\x82 mode: " << permission_mode_label(state.permission_mode);
    }
    return output.str();
}

auto render_composer_hint(bool busy, int history_index) -> std::string
{
    if (busy)
    {
        return "esc stop · ctrl+c stop";
    }
    auto hint = std::string{"shift+enter newline · enter send · / commands · ctrl+p/n history · ctrl+c exit"};
    if (history_index >= 0)
    {
        hint += " · history " + std::to_string(history_index + 1);
    }
    return hint;
}

auto busy_spinner_frame(int frame) -> std::string
{
    static constexpr std::string_view frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    return std::string{frames[static_cast<std::size_t>(frame) % (sizeof(frames) / sizeof(frames[0]))]};
}

auto welcome_banner_element(const TuiDisplayConfig& config) -> Element
{
    const auto lines = render_welcome_lines(config);
    Elements rows;
    rows.push_back(text(std::string{lines.at(0)}) | color(TuiTheme::primary()) | bold);
    rows.push_back(text(lines.at(1)) | dim);
    rows.push_back(text(" "));
    rows.push_back(hbox({
        text(" "),
        text("/help") | color(TuiTheme::primary()),
        text(" commands") | dim,
        text("  |  ") | dim,
        text("/skills") | color(TuiTheme::primary()),
        text(" list") | dim,
        text("  |  ") | dim,
        text("Ctrl+C") | color(TuiTheme::primary()),
        text(" exit") | dim,
    }));
    return vbox(std::move(rows));
}

auto transcript_item_element(const TranscriptItem& item, int width) -> Element
{
    if (item.kind == "user")
    {
        Elements rows;
        for (const auto& line : split_lines(item.text))
        {
            rows.push_back(hbox({text("> ") | dim, text(line)}));
        }
        return rows.empty() ? text("") : vbox(std::move(rows));
    }
    if (item.kind == "assistant")
    {
        return markdown::render_text(item.text, width);
    }
    if (item.kind == "system")
    {
        return hbox({text("[system]") | color(TuiTheme::warning()), text(" " + item.text)});
    }
    if (item.kind == "tool")
    {
        Elements rows;
        auto summary = text("• " + tool_summary_text(item)) | dim;
        if (item.is_error)
        {
            summary = summary | color(TuiTheme::error());
        }
        rows.push_back(summary);
        if (item.is_error && item.expanded && !item.detail.empty())
        {
            const auto error_lines = split_lines(item.detail);
            const auto visible = std::min(error_lines.size(), static_cast<std::size_t>(k_tool_error_max_lines));
            for (std::size_t index = 0; index < visible; ++index)
            {
                const auto prefix = index + 1 == visible ? "└ " : "│ ";
                rows.push_back(text(prefix + error_lines.at(index)) | color(TuiTheme::error()));
            }
            if (error_lines.size() > visible)
            {
                rows.push_back(text("└ ... (" + std::to_string(error_lines.size() - visible) + " more lines)") | dim);
            }
        }
        else if (!item.detail.empty() && item.expanded)
        {
            rows.push_back(paragraphAlignLeft(item.detail) | dim);
        }
        return vbox(std::move(rows));
    }
    if (item.kind == "error")
    {
        return text(item.text) | color(TuiTheme::error());
    }
    return text(item.kind + ": " + item.text);
}

auto command_palette_element(const CommandPaletteState& palette, int width) -> Element
{
    Elements rows;
    rows.push_back(text(horizontal_rule(width)) | color(TuiTheme::primary()));

    auto title = std::string{"Select a command"};
    if (palette.query.empty())
    {
        title += "  (type to search)";
    }
    rows.push_back(text(title) | color(TuiTheme::primary()) | bold);
    rows.push_back(text(palette.query.empty() ? "↑↓ navigate · Enter select · Esc cancel"
                                              : "↑↓ navigate · Enter select · Esc cancel · Backspace clear")
                   | dim);

    rows.push_back(text(" "));

    if (!palette.query.empty())
    {
        rows.push_back(hbox({text("Search: ") | color(TuiTheme::primary()), text(palette.query)}));
    }

    const auto visible = std::min(palette.matches.size(), static_cast<std::size_t>(k_command_palette_page_size));
    if (palette.matches.empty())
    {
        rows.push_back(text("No matches") | dim);
    }
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto command_index = palette.matches.at(row);
        const auto& command = palette.commands.at(command_index);
        const auto pointer = row == palette.cursor ? std::string{k_select_pointer} : "  ";
        Element name = text("/" + command.name);
        if (row == palette.cursor)
        {
            name = name | color(TuiTheme::primary()) | bold;
        }
        rows.push_back(hbox({
            text(pointer),
            std::move(name),
            text("  " + command.description) | dim,
        }));
    }
    if (palette.matches.size() > visible)
    {
        rows.push_back(text("▼ " + std::to_string(palette.matches.size() - visible) + " more") | dim);
    }

    rows.push_back(text(" "));
    rows.push_back(text(horizontal_rule(width)) | color(TuiTheme::primary()));
    return vbox(std::move(rows));
}

auto permission_modal_element(const PermissionPrompt& prompt, int width) -> Element
{
    Elements rows;
    rows.push_back(hbox({
        text("┌ Allow ") | color(TuiTheme::warning()) | bold,
        text(prompt.tool_name) | color(TuiTheme::primary()) | bold,
        text("?") | bold,
    }));
    if (!prompt.reason.empty())
    {
        rows.push_back(hbox({text("│ ") | color(TuiTheme::warning()), text(prompt.reason) | dim}));
    }
    if (prompt.command)
    {
        rows.push_back(hbox({text("│ command: ") | color(TuiTheme::warning()) | dim, text(*prompt.command)}));
    }
    if (prompt.path)
    {
        rows.push_back(hbox({text("│ path: ") | color(TuiTheme::warning()) | dim, text(prompt.path->string())}));
    }
    rows.push_back(hbox({
        text("└ ") | color(TuiTheme::warning()),
        text("[y] Allow once") | color(TuiTheme::success()),
        text("  "),
        text("[a] Allow session") | color(TuiTheme::success()),
        text("  "),
        text("[n] Deny") | color(TuiTheme::error()),
    }));
    return vbox(std::move(rows));
}

auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> Element
{
    Elements parts;

    // model label
    parts.push_back(text("model: ") | color(TuiTheme::primary()) | dim);
    parts.push_back(text(config.model) | dim);
    parts.push_back(text(std::string{k_separator}) | dim);

    // provider
    parts.push_back(text("provider: ") | dim);
    parts.push_back(text(config.provider_type) | dim);

    // token usage
    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("tokens: ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.input_tokens) + "\xe2\x86\x93 ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.output_tokens) + "\xe2\x86\x91") | dim);
    }

    // skills
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

    // mcp connections
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

    // mode (hidden in plan mode — the badge replaces it)
    if (state.permission_mode != PermissionMode::Plan)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("mode: " + permission_mode_label(state.permission_mode)) | dim);
    }

    if (state.permission_mode == PermissionMode::Plan)
    {
        parts.push_back(text(" [PLAN MODE] ") | color(TuiTheme::warning()) | bold);
    }
    return hbox(std::move(parts));
}

auto select_modal_element(const SelectModalState& modal, int width) -> Element
{
    Elements rows;

    // Title
    rows.push_back(text(modal.title) | color(TuiTheme::primary()) | bold);
    rows.push_back(text(" "));

    // Options
    for (std::size_t index = 0; index < modal.options.size(); ++index)
    {
        const auto& option = modal.options.at(index);
        const auto is_selected = index == modal.cursor;

        auto pointer = is_selected ? std::string{k_select_pointer} : std::string{"  "};
        auto name = text(pointer + option.label);
        if (is_selected)
        {
            name = name | color(TuiTheme::primary()) | bold;
        }

        Elements row_parts;
        row_parts.push_back(std::move(name));

        if (option.is_current)
        {
            row_parts.push_back(text(std::string{k_current_mark}) | color(TuiTheme::success()));
        }

        if (!option.description.empty())
        {
            row_parts.push_back(text("  " + option.description) | dim);
        }

        rows.push_back(hbox(std::move(row_parts)));
    }

    // Navigation hints
    rows.push_back(text(" "));
    rows.push_back(text("\xe2\x86\x91\xe2\x86\x93"" navigate  \xe2\x8f\x8e"" select  esc cancel  1-9 quick select") | dim);

    return vbox(std::move(rows)) | borderRounded | color(TuiTheme::primary());
}

auto question_modal_element(const QuestionModalState& modal, int width) -> Element
{
    Elements rows;

    // Waiting animation (static — the spinner animates in the main loop)
    rows.push_back(text("\xe2\x8f\xb3 Agent is waiting for your input...") | color(TuiTheme::warning()) | dim);

    rows.push_back(text(" "));
    rows.push_back(hbox({
        text("\xe2\x9d\x93 ") | color(TuiTheme::warning()) | bold,
        text(modal.question) | bold,
    }));

    if (!modal.tool_name.empty())
    {
        rows.push_back(hbox({
            text("  Tool: ") | dim,
            text(modal.tool_name) | color(TuiTheme::primary()),
        }));
    }
    if (!modal.reason.empty())
    {
        rows.push_back(hbox({text("  Reason: ") | dim, text(modal.reason) | dim}));
    }

    // Previous lines
    if (!modal.extra_lines.empty())
    {
        rows.push_back(text(" "));
        for (const auto& line : modal.extra_lines)
        {
            rows.push_back(text("  " + line) | dim);
        }
    }

    // Input line with cursor
    rows.push_back(text(" "));
    auto input_text = modal.answer.empty() ? std::string{" "} : modal.answer;
    Element input_element;
    if (modal.answer.empty())
    {
        input_element = hbox({
            text("> ") | color(TuiTheme::primary()),
            text(" ") | inverted,
        });
    }
    else
    {
        input_element = hbox({
            text("> ") | color(TuiTheme::primary()),
            text(modal.answer.substr(0, modal.answer.size() - 1)),
            text(std::string(1, modal.answer.back())) | inverted,
        });
    }
    rows.push_back(input_element);

    rows.push_back(text("  shift+enter newline | enter submit") | dim);

    return vbox(std::move(rows)) | borderDouble | color(TuiTheme::warning());
}

} // namespace codeharness::tui::render

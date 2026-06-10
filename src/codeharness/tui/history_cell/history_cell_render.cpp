#include "codeharness/tui/history_cell/history_cell_render.h"

#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_markdown.h"
#include "codeharness/tui/tui_theme.h"

#include <sstream>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

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

auto should_show_live_tool_input(const TranscriptItem& item) -> bool
{
    return item.kind == HistoryCellKind::tool && item.live && !item.input_json.empty() && item.detail.empty();
}

/// Get the color for a tool based on its status.
auto tool_status_color(const TranscriptItem& item) -> ftxui::Color
{
    if (item.tool_status == ToolStatus::running)
    {
        return TuiTheme::tool_running();
    }
    if (item.is_error || item.tool_status == ToolStatus::failed)
    {
        return TuiTheme::tool_failed();
    }
    return TuiTheme::tool_completed();
}

/// Create a tree prefix for tool details display.
auto tool_tree_prefix(std::size_t index, std::size_t total, std::size_t max_lines) -> std::string
{
    const auto visible = std::min(total, max_lines);
    if (index + 1 == visible)
    {
        return std::string{k_tree_last};
    }
    return std::string{k_tree_branch};
}

/// Render tool details with tree-drawing characters, limited to max lines.
auto render_tool_details(const TranscriptItem& item, int width) -> Elements
{
    Elements rows;

    // Show live tool input if applicable
    if (should_show_live_tool_input(item))
    {
        rows.push_back(hbox({
            text("  input: ") | muted_style(),
            text(item.input_json) | dim,
        }));
        return rows;
    }

    // Show error details with tree lines (limited)
    if (item.is_error && item.expanded && !item.detail.empty())
    {
        const auto error_lines = split_lines(item.detail);
        const auto visible = std::min(error_lines.size(), static_cast<std::size_t>(k_tool_error_max_lines));
        for (std::size_t i = 0; i < visible; ++i)
        {
            rows.push_back(hbox({
                text(tool_tree_prefix(i, error_lines.size(), k_tool_error_max_lines)) | color(TuiTheme::error()),
                text(error_lines.at(i)) | color(TuiTheme::error()),
            }));
        }
        if (error_lines.size() > visible)
        {
            rows.push_back(
                text(std::string{k_tree_last} + " ... (" + std::to_string(error_lines.size() - visible) +
                     " more lines)") |
                dim);
        }
        return rows;
    }

    // Show expanded tool output
    if (item.expanded && !item.detail.empty())
    {
        for (const auto& line : split_lines(item.detail))
        {
            rows.push_back(text(trim_to_width(line, width)) | dim);
        }
        return rows;
    }

    return rows;
}

} // namespace

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

auto render_history_cell_lines(const TranscriptItem& item, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    if (item.kind == HistoryCellKind::user)
    {
        for (const auto& line : split_lines(item.text))
        {
            lines.push_back("> " + trim_to_width(line, width > 2 ? width - 2 : width));
        }
        return lines;
    }
    if (item.kind == HistoryCellKind::assistant)
    {
        const auto rendered = markdown::render_plain_text(item.text, width);
        for (const auto& line : split_lines(rendered))
        {
            lines.push_back(trim_to_width(line, width));
        }
        return lines;
    }
    if (item.kind == HistoryCellKind::system)
    {
        lines.push_back("[system] " + trim_to_width(item.text, width > 9 ? width - 9 : width));
        return lines;
    }
    if (item.kind == HistoryCellKind::tool)
    {
        lines.push_back("• " + trim_to_width(tool_summary_text(item), width > 2 ? width - 2 : width));
        if (should_show_live_tool_input(item))
        {
            lines.push_back("  input: " + trim_to_width(item.input_json, width > 9 ? width - 9 : width));
        }
        if (item.expanded && !item.detail.empty())
        {
            if (item.is_error)
            {
                const auto error_lines = split_lines(item.detail);
                const auto visible = std::min(error_lines.size(), static_cast<std::size_t>(k_tool_error_max_lines));
                for (std::size_t index = 0; index < visible; ++index)
                {
                    const auto prefix = index + 1 == visible ? "└ " : "├ ";
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
        return lines;
    }
    if (item.kind == HistoryCellKind::error)
    {
        lines.push_back(trim_to_width(item.text, width));
    }
    return lines;
}

auto history_cell_element(const TranscriptItem& item, int width) -> Element
{
    if (item.kind == HistoryCellKind::user)
    {
        Elements rows;
        for (const auto& line : split_lines(item.text))
        {
            rows.push_back(hbox({text("> "), text(line)}));
        }
        return rows.empty() ? text("") : vbox(std::move(rows));
    }

    if (item.kind == HistoryCellKind::assistant)
    {
        return markdown::render_text(item.text, width);
    }

    if (item.kind == HistoryCellKind::system)
    {
        return hbox({text("[system]") | color(TuiTheme::warning()), text(" " + item.text)});
    }

    if (item.kind == HistoryCellKind::tool)
    {
        Elements rows;

        // Tool header with status-appropriate color and bullet
        const auto status_color = tool_status_color(item);
        auto summary = styled_bullet(tool_summary_text(item), item.is_error);
        if (item.tool_status == ToolStatus::running)
        {
            summary = hbox({
                text("\xe2\x80\xa2 ") | color(status_color),
                text(tool_summary_text(item)) | color(status_color),
            });
        }
        rows.push_back(summary);

        // Render details subtree
        const auto detail_rows = render_tool_details(item, width);
        for (const auto& detail : detail_rows)
        {
            rows.push_back(detail);
        }

        return vbox(std::move(rows));
    }

    if (item.kind == HistoryCellKind::error)
    {
        return text(item.text) | error_style();
    }

    return text(std::string{history_cell_kind_name(item.kind)} + ": " + item.text);
}

} // namespace codeharness::tui::render

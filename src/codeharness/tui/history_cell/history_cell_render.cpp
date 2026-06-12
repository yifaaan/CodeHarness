#include "codeharness/tui/history_cell/history_cell_render.h"

#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_markdown.h"
#include "codeharness/tui/tui_theme.h"

#include <algorithm>
#include <sstream>
#include <string>

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
        lines.push_back(std::move(line));
    }
    return lines;
}

auto tool_status_color(const TranscriptItem& item) -> ftxui::Color
{
    if (item.tool_status == ToolStatus::running)
    {
        return TuiTheme::codex_bullet();
    }
    if (item.is_error || item.tool_status == ToolStatus::failed)
    {
        return TuiTheme::error();
    }
    return TuiTheme::success();
}

auto render_tool_output(const std::vector<std::string>& all_lines, int width) -> Elements
{
    Elements rows;
    if (all_lines.empty())
    {
        rows.push_back(hbox({
            text("  \xe2\x94\x94 ") | color(TuiTheme::codex_output_dim()) | dim,
            text("(no output)") | color(TuiTheme::codex_output_dim()) | dim,
        }));
        return rows;
    }

    const auto total = all_lines.size();
    constexpr std::size_t line_limit = k_codex_tool_max_lines;
    const bool show_ellipsis = total > 2 * line_limit;

    const auto head_end = std::min(total, line_limit);
    for (std::size_t i = 0; i < head_end; ++i)
    {
        const bool is_last_line = (i + 1 == head_end) && !show_ellipsis && (head_end == total);
        const std::string prefix = is_last_line ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
        rows.push_back(hbox({
            text(prefix) | color(TuiTheme::codex_output_dim()) | dim,
            text(trim_to_width(all_lines.at(i), std::max(0, width - 4))) | color(TuiTheme::codex_output_dim()) | dim,
        }));
    }

    if (show_ellipsis)
    {
        rows.push_back(hbox({
            text("  \xe2\x80\xa6 +" + std::to_string(total - 2 * line_limit) + " lines (" +
                 std::string{k_transcript_hint} + ")") |
                color(TuiTheme::codex_output_dim()) | dim,
        }));

        const auto tail_start = total - line_limit;
        for (std::size_t i = tail_start; i < total; ++i)
        {
            const bool is_last = (i + 1 == total);
            const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
            rows.push_back(hbox({
                text(prefix) | color(TuiTheme::codex_output_dim()) | dim,
                text(trim_to_width(all_lines.at(i), std::max(0, width - 4))) | color(TuiTheme::codex_output_dim()) | dim,
            }));
        }
    }
    else if (head_end < total)
    {
        for (std::size_t i = head_end; i < total; ++i)
        {
            const bool is_last = (i + 1 == total);
            const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
            rows.push_back(hbox({
                text(prefix) | color(TuiTheme::codex_output_dim()) | dim,
                text(trim_to_width(all_lines.at(i), std::max(0, width - 4))) | color(TuiTheme::codex_output_dim()) | dim,
            }));
        }
    }

    return rows;
}

auto render_error_output(const std::vector<std::string>& err_lines, int width) -> Elements
{
    Elements rows;
    constexpr std::size_t visible = k_tool_error_max_lines;
    const auto show_count = std::min(err_lines.size(), visible);

    for (std::size_t i = 0; i < show_count; ++i)
    {
        const bool is_last = (i + 1 == show_count);
        const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
        rows.push_back(hbox({
            text(prefix) | color(TuiTheme::error()) | bold,
            text(trim_to_width(err_lines.at(i), std::max(0, width - 4))) | color(TuiTheme::error()),
        }));
    }

    if (err_lines.size() > visible)
    {
        rows.push_back(hbox({
            text("  \xe2\x80\xa6 +" + std::to_string(err_lines.size() - visible) + " more lines") | color(TuiTheme::error()) | dim,
        }));
    }

    return rows;
}

} // anonymous namespace

auto tool_line_count(std::string_view detail) -> int
{
    return static_cast<int>(split_lines(detail).size());
}

auto tool_summary_text(const TranscriptItem& item) -> std::string
{
    if (!item.summary_text.empty())
    {
        return item.summary_text;
    }
    if (!item.text.empty())
    {
        return item.text;
    }

    auto label = item.label;
    if (label.empty())
    {
        label = item.id.empty() ? "tool" : item.id;
    }

    std::ostringstream summary;
    if (item.tool_status == ToolStatus::running)
    {
        summary << "Running " << label;
    }
    else
    {
        summary << "Ran " << label;
        if (item.is_error || item.tool_status == ToolStatus::failed)
        {
            summary << " error";
        }
    }

    if (!item.duration_label.empty())
    {
        summary << ' ' << item.duration_label;
    }
    return summary.str();
}

auto render_history_cell_lines(const TranscriptItem& item, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;

    if (item.kind == HistoryCellKind::user)
    {
        if (item.text.empty())
        {
            return lines;
        }
        const auto text_lines = split_lines(item.text);
        if (text_lines.empty())
        {
            return lines;
        }
        lines.push_back("");
        for (std::size_t i = 0; i < text_lines.size(); ++i)
        {
            const std::string prefix = (i == 0) ? "\xe2\x80\xba " : "  ";
            lines.push_back(prefix + trim_to_width(text_lines.at(i), std::max(0, width - 4)));
        }
        lines.push_back("");
        return lines;
    }

    if (item.kind == HistoryCellKind::tool)
    {
        lines.push_back(std::string{k_codex_bullet} + tool_summary_text(item));

        if (item.tool_status == ToolStatus::running && !item.input_json.empty() &&
            item.detail.empty() && item.output_text.empty() && item.stderr_text.empty())
        {
            lines.push_back("  " + trim_to_width("input: " + item.input_json, std::max(0, width - 2)));
            return lines;
        }

        const auto error_text = item.stderr_text.empty() ? item.detail : item.stderr_text;
        const auto output_text = item.output_text.empty() ? item.detail : item.output_text;
        if ((item.tool_status == ToolStatus::completed || item.tool_status == ToolStatus::failed || item.is_error) &&
            (!output_text.empty() || !error_text.empty()))
        {
            if (item.is_error)
            {
                const auto err_lines = split_lines(error_text);
                if (!err_lines.empty())
                {
                    const auto visible = std::min(err_lines.size(), static_cast<std::size_t>(k_tool_error_max_lines));
                    for (std::size_t i = 0; i < visible; ++i)
                    {
                        const bool is_last = (i + 1 == visible);
                        const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
                        lines.push_back(prefix + trim_to_width(err_lines.at(i), std::max(0, width - 4)));
                    }
                    if (err_lines.size() > visible)
                    {
                        lines.push_back("  \xe2\x80\xa6 +" + std::to_string(err_lines.size() - visible) + " more lines");
                    }
                }
                else
                {
                    lines.push_back("  \xe2\x94\x94 (no output)");
                }
            }
            else
            {
                const auto out_lines = split_lines(output_text);
                if (out_lines.empty())
                {
                    lines.push_back("  \xe2\x94\x94 (no output)");
                }
                else
                {
                    const auto total = out_lines.size();
                    constexpr std::size_t line_limit = k_codex_tool_max_lines;
                    const bool show_ellipsis = total > 2 * line_limit;

                    const auto head_end = std::min(total, line_limit);
                    for (std::size_t i = 0; i < head_end; ++i)
                    {
                        const bool is_last_line = (i + 1 == head_end) && !show_ellipsis && (head_end == total);
                        const std::string prefix = is_last_line ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
                        lines.push_back(prefix + trim_to_width(out_lines.at(i), std::max(0, width - 4)));
                    }

                    if (show_ellipsis)
                    {
                        lines.push_back("  \xe2\x80\xa6 +" + std::to_string(total - 2 * line_limit) +
                                        " lines (" + std::string{k_transcript_hint} + ")");
                        const auto tail_start = total - line_limit;
                        for (std::size_t i = tail_start; i < total; ++i)
                        {
                            const bool is_last = (i + 1 == total);
                            const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
                            lines.push_back(prefix + trim_to_width(out_lines.at(i), std::max(0, width - 4)));
                        }
                    }
                    else if (head_end < total)
                    {
                        for (std::size_t i = head_end; i < total; ++i)
                        {
                            const bool is_last = (i + 1 == total);
                            const std::string prefix = is_last ? "  \xe2\x94\x94 " : "  \xe2\x94\x82 ";
                            lines.push_back(prefix + trim_to_width(out_lines.at(i), std::max(0, width - 4)));
                        }
                    }
                }
            }
        }
        return lines;
    }

    if (item.kind == HistoryCellKind::system)
    {
        lines.push_back(std::string{k_codex_bullet} + trim_to_width(item.text, std::max(0, width - 2)));
        return lines;
    }

    if (item.kind == HistoryCellKind::error)
    {
        lines.push_back("  \xe2\x9c\x95 " + trim_to_width(item.text, std::max(0, width - 5)));
        return lines;
    }

    // Assistant
    const auto rendered = markdown::render_plain_text(item.text, std::max(0, width - 2));
    for (const auto& line : split_lines(rendered))
    {
        if (lines.empty())
        {
            lines.push_back("\xe2\x80\xa2 " + trim_to_width(line, std::max(0, width - 2)));
        }
        else
        {
            lines.push_back("  " + trim_to_width(line, std::max(0, width - 2)));
        }
    }
    return lines;
}

auto history_cell_element(const TranscriptItem& item, int width) -> Element
{
    if (item.kind == HistoryCellKind::user)
    {
        Elements rows;
        const auto text_lines = split_lines(item.text);
        if (text_lines.empty())
        {
            return text("");
        }

        // Top padding — full width via hbox + filler
        rows.push_back(hbox({
            text("  "),
            filler(),
        }));
        for (std::size_t i = 0; i < text_lines.size(); ++i)
        {
            const auto content = trim_to_width(text_lines.at(i), std::max(0, width - 4));
            if (i == 0)
            {
                rows.push_back(hbox({
                    text("\xe2\x80\xba ") | color(TuiTheme::codex_user_prefix()) | bold | dim,
                    text(content),
                    filler(),
                }));
            }
            else
            {
                rows.push_back(hbox({
                    text("  "),
                    text(content),
                    filler(),
                }));
            }
        }
        // Bottom padding — full width via hbox + filler
        rows.push_back(hbox({
            text("  "),
            filler(),
        }));
        return vbox(std::move(rows)) | bgcolor(TuiTheme::user_message_bg());
    }

    if (item.kind == HistoryCellKind::assistant)
    {
        return hbox({
            text("\xe2\x80\xa2 ") | color(TuiTheme::codex_bullet()) | dim,
            markdown::render_text(item.text, std::max(0, width - 2)),
        });
    }

    if (item.kind == HistoryCellKind::system)
    {
        return hbox({
            text(std::string{k_codex_bullet}) | color(TuiTheme::codex_bullet()) | dim,
            text(item.text) | color(TuiTheme::text_muted()),
        });
    }

    if (item.kind == HistoryCellKind::tool)
    {
        Elements rows;
        const auto status_color = tool_status_color(item);

        rows.push_back(hbox({
            text("\xe2\x80\xa2 ") | color(status_color) | bold,
            styled_line_element(codex_command_header_segments(tool_summary_text(item))),
        }));

        if (item.tool_status == ToolStatus::running && !item.input_json.empty() &&
            item.detail.empty() && item.output_text.empty() && item.stderr_text.empty())
        {
            rows.push_back(hbox({
                text("  input: ") | color(TuiTheme::codex_output_dim()),
                text(item.input_json) | color(TuiTheme::codex_output_dim()),
            }));
        }
        else if (item.is_error)
        {
            const auto err_lines = split_lines(item.stderr_text.empty() ? item.detail : item.stderr_text);
            if (!err_lines.empty())
            {
                const auto err_rows = render_error_output(err_lines, width);
                for (const auto& row : err_rows)
                {
                    rows.push_back(row);
                }
            }
        }
        else if (!item.output_text.empty() || item.tool_status == ToolStatus::completed)
        {
            const auto out_lines = split_lines(item.output_text.empty() ? item.detail : item.output_text);
            const auto out_rows = render_tool_output(out_lines, width);
            for (const auto& row : out_rows)
            {
                rows.push_back(row);
            }
        }

        return vbox(std::move(rows));
    }

    if (item.kind == HistoryCellKind::error)
    {
        return hbox({
            text("  \xe2\x9c\x95 ") | color(TuiTheme::error()),
            text(item.text) | color(TuiTheme::error()),
        });
    }

    return text(std::string{history_cell_kind_name(item.kind)} + ": " + item.text);
}

} // namespace codeharness::tui::render

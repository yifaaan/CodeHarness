#include "codeharness/tui/tui_markdown.h"

#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <sstream>
#include <string_view>

namespace codeharness::tui::markdown
{
namespace
{

using namespace ftxui;

struct InlineSegment
{
    std::string text;
    bool bold = false;
    bool italic = false;
    bool code = false;
};

auto trim_right(std::string_view value) -> std::string_view
{
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    {
        value.remove_suffix(1);
    }
    return value;
}

auto split_lines_view(std::string_view text) -> std::vector<std::string_view>
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size())
    {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

auto parse_inline_segments(std::string_view line) -> std::vector<InlineSegment>
{
    std::vector<InlineSegment> segments;
    std::size_t index = 0;
    while (index < line.size())
    {
        if (line.substr(index).starts_with("**"))
        {
            const auto end = line.find("**", index + 2);
            if (end != std::string_view::npos)
            {
                segments.push_back(InlineSegment{.text = std::string{line.substr(index + 2, end - index - 2)}, .bold = true});
                index = end + 2;
                continue;
            }
        }
        if (line[index] == '`')
        {
            const auto end = line.find('`', index + 1);
            if (end != std::string_view::npos)
            {
                segments.push_back(InlineSegment{.text = std::string{line.substr(index + 1, end - index - 1)}, .code = true});
                index = end + 1;
                continue;
            }
        }
        if (line[index] == '*' && (index + 1 >= line.size() || line[index + 1] != '*'))
        {
            const auto end = line.find('*', index + 1);
            if (end != std::string_view::npos)
            {
                segments.push_back(InlineSegment{.text = std::string{line.substr(index + 1, end - index - 1)}, .italic = true});
                index = end + 1;
                continue;
            }
        }

        const auto next_special = std::min({
            line.find("**", index) == std::string_view::npos ? line.size() : line.find("**", index),
            line.find('`', index) == std::string_view::npos ? line.size() : line.find('`', index),
            line.find('*', index) == std::string_view::npos ? line.size() : line.find('*', index),
        });
        if (next_special > index)
        {
            segments.push_back(InlineSegment{.text = std::string{line.substr(index, next_special - index)}});
            index = next_special;
            continue;
        }
        segments.push_back(InlineSegment{.text = std::string{line.substr(index, 1)}});
        ++index;
    }
    return segments;
}

auto inline_element(std::string_view line) -> Element
{
    Elements parts;
    for (const auto& segment : parse_inline_segments(line))
    {
        Element part = text(segment.text);
        if (segment.bold)
        {
            part = part | bold;
        }
        if (segment.italic)
        {
            part = part | dim;
        }
        if (segment.code)
        {
            part = part | color(TuiTheme::accent());
        }
        parts.push_back(std::move(part));
    }
    if (parts.empty())
    {
        return text("");
    }
    return hbox(std::move(parts));
}

auto heading_level(std::string_view line, std::string_view marker, BlockKind kind) -> std::optional<Block>
{
    if (!line.starts_with(marker))
    {
        return std::nullopt;
    }
    auto body = line.substr(marker.size());
    while (!body.empty() && (body.front() == ' ' || body.front() == '\t'))
    {
        body.remove_prefix(1);
    }
    return Block{.kind = kind, .text = std::string{body}};
}

auto is_separator_line(std::string_view line) -> bool
{
    if (line.empty())
    {
        return false;
    }
    char delimiter = '\0';
    for (const auto character : line)
    {
        if (character == ' ' || character == '\t')
        {
            continue;
        }
        if (delimiter == '\0')
        {
            if (character == '-' || character == '*' || character == '_')
            {
                delimiter = character;
            }
            else
            {
                return false;
            }
        }
        else if (character != delimiter)
        {
            return false;
        }
    }
    return delimiter != '\0';
}

// Parse a pipe-delimited row: "| a | b | c |" → {"a", "b", "c"}
auto parse_pipe_row(std::string_view line) -> std::vector<std::string>
{
    // Strip leading/trailing pipes
    auto trimmed = line;
    while (!trimmed.empty() && trimmed.front() == '|')
    {
        trimmed.remove_prefix(1);
    }
    while (!trimmed.empty() && trimmed.back() == '|')
    {
        trimmed.remove_suffix(1);
    }

    std::vector<std::string> cells;
    std::size_t start = 0;
    while (start <= trimmed.size())
    {
        auto end = trimmed.find('|', start);
        if (end == std::string_view::npos)
        {
            auto cell = std::string{trimmed.substr(start)};
            // trim whitespace
            auto cell_view = std::string_view{cell};
            while (!cell_view.empty() && (cell_view.front() == ' ' || cell_view.front() == '\t'))
            {
                cell_view.remove_prefix(1);
            }
            while (!cell_view.empty() && (cell_view.back() == ' ' || cell_view.back() == '\t'))
            {
                cell_view.remove_suffix(1);
            }
            cells.push_back(std::string{cell_view});
            break;
        }
        auto cell = std::string{trimmed.substr(start, end - start)};
        auto cell_view = std::string_view{cell};
        while (!cell_view.empty() && (cell_view.front() == ' ' || cell_view.front() == '\t'))
        {
            cell_view.remove_prefix(1);
        }
        while (!cell_view.empty() && (cell_view.back() == ' ' || cell_view.back() == '\t'))
        {
            cell_view.remove_suffix(1);
        }
        cells.push_back(std::string{cell_view});
        start = end + 1;
    }
    return cells;
}

auto is_separator_row(const std::vector<std::string>& cells) -> bool
{
    for (const auto& cell : cells)
    {
        auto view = std::string_view{cell};
        bool found_colon = false;
        bool found_dash = false;
        for (const auto ch : view)
        {
            if (ch == '-' || ch == ':')
            {
                if (ch == ':')
                {
                    found_colon = true;
                }
                found_dash = true;
            }
            else if (ch != ' ')
            {
                return false;
            }
        }
        if (!found_dash)
        {
            return false;
        }
    }
    return true;
}

auto compute_col_widths_impl(const TableBlock& table) -> std::vector<int>
{
    auto col_count = table.header.cells.size();
    for (const auto& row : table.rows)
    {
        col_count = std::max(col_count, row.cells.size());
    }

    std::vector<int> widths(col_count, 0);
    for (std::size_t c = 0; c < table.header.cells.size(); ++c)
    {
        widths.at(c) = std::max(widths.at(c), static_cast<int>(table.header.cells.at(c).text.size()));
    }
    for (const auto& row : table.rows)
    {
        for (std::size_t c = 0; c < row.cells.size(); ++c)
        {
            widths.at(c) = std::max(widths.at(c), static_cast<int>(row.cells.at(c).text.size()));
        }
    }
    return widths;
}

auto render_table_element(const TableBlock& table) -> Element
{
    auto col_widths = compute_col_widths_impl(table);
    auto col_count = col_widths.size();

    // Build box-drawing characters
    auto top = std::string{"\xe2\x94\x8c"}; // ┌
    auto mid = std::string{"\xe2\x94\x9c"}; // ├
    auto bot = std::string{"\xe2\x94\x94"}; // └

    for (std::size_t c = 0; c < col_count; ++c)
    {
        auto dash_count = col_widths.at(c) + 2;
        top += std::string(static_cast<std::size_t>(dash_count), '\xe2\x94\x80'); // ─
        mid += std::string(static_cast<std::size_t>(dash_count), '\xe2\x94\x80');
        bot += std::string(static_cast<std::size_t>(dash_count), '\xe2\x94\x80');
        if (c + 1 < col_count)
        {
            top += "\xe2\x94\xac"; // ┬
            mid += "\xe2\x94\xbc"; // ┼
            bot += "\xe2\x94\xb4"; // ┴
        }
    }
    top += "\xe2\x94\x90"; // ┐
    mid += "\xe2\x94\xa4"; // ┤
    bot += "\xe2\x94\x98"; // ┘

    Elements rows;
    rows.push_back(text(top) | color(TuiTheme::text_dim()));

    // Header row
    Elements header_parts;
    header_parts.push_back(text("\xe2\x94\x82") | color(TuiTheme::text_dim())); // │
    for (std::size_t c = 0; c < col_count; ++c)
    {
        auto cell_text = c < table.header.cells.size() ? table.header.cells.at(c).text : std::string{};
        auto padding = static_cast<int>(cell_text.size()) < col_widths.at(c)
                           ? col_widths.at(c) - static_cast<int>(cell_text.size())
                           : 0;
        header_parts.push_back(text(" " + cell_text + std::string(static_cast<std::size_t>(padding), ' ') + " ") |
                               bold | color(TuiTheme::primary()));
        header_parts.push_back(text("\xe2\x94\x82") | color(TuiTheme::text_dim())); // │
    }
    rows.push_back(hbox(std::move(header_parts)));
    rows.push_back(text(mid) | color(TuiTheme::text_dim()));

    // Data rows
    for (const auto& row : table.rows)
    {
        Elements row_parts;
        row_parts.push_back(text("\xe2\x94\x82") | color(TuiTheme::text_dim())); // │
        for (std::size_t c = 0; c < col_count; ++c)
        {
            auto cell_text = c < row.cells.size() ? row.cells.at(c).text : std::string{};
            auto padding = static_cast<int>(cell_text.size()) < col_widths.at(c)
                               ? col_widths.at(c) - static_cast<int>(cell_text.size())
                               : 0;
            row_parts.push_back(text(" " + cell_text + std::string(static_cast<std::size_t>(padding), ' ') + " "));
            row_parts.push_back(text("\xe2\x94\x82") | color(TuiTheme::text_dim())); // │
        }
        rows.push_back(hbox(std::move(row_parts)));
    }

    rows.push_back(text(bot) | color(TuiTheme::text_dim()));

    return vbox(std::move(rows));
}

} // namespace

auto parse_table(std::string_view markdown, std::size_t start_line) -> std::pair<TableBlock, std::size_t>
{
    auto lines = split_lines_view(markdown);
    if (start_line >= lines.size())
    {
        return {};
    }

    // First line should be the header row with pipes
    auto header_line = lines.at(start_line);
    if (header_line.find('|') == std::string_view::npos)
    {
        return {};
    }

    auto header_cells = parse_pipe_row(header_line);
    if (header_cells.empty())
    {
        return {};
    }

    // Second line should be the separator
    if (start_line + 1 >= lines.size())
    {
        return {};
    }
    auto separator_cells = parse_pipe_row(lines.at(start_line + 1));
    if (!is_separator_row(separator_cells))
    {
        return {};
    }

    // Parse data rows
    TableBlock table;
    for (const auto& cell : header_cells)
    {
        table.header.cells.push_back(TableCell{.text = cell});
    }

    std::size_t current = start_line + 2;
    while (current < lines.size())
    {
        auto line_view = lines.at(current);
        // Trim trailing whitespace
        while (!line_view.empty() && (line_view.back() == ' ' || line_view.back() == '\t' || line_view.back() == '\r'))
        {
            line_view.remove_suffix(1);
        }
        if (line_view.empty() || line_view.find('|') == std::string_view::npos)
        {
            break;
        }
        auto cells = parse_pipe_row(line_view);
        TableRow row;
        for (auto& cell : cells)
        {
            row.cells.push_back(TableCell{.text = std::move(cell)});
        }
        table.rows.push_back(std::move(row));
        ++current;
    }

    table.col_widths = compute_col_widths_impl(table);
    return {std::move(table), current};
}

auto compute_col_widths(const TableBlock& table) -> std::vector<int>
{
    return compute_col_widths_impl(table);
}

auto parse_blocks(std::string_view markdown) -> std::vector<Block>
{
    std::vector<Block> blocks;
    bool in_code_fence = false;
    std::string code_buffer;
    std::string code_language;

    auto lines = split_lines_view(markdown);
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index)
    {
        auto raw_line = lines.at(line_index);
        // Strip \r
        if (!raw_line.empty() && raw_line.back() == '\r')
        {
            raw_line.remove_suffix(1);
        }

        // Code fence handling (uses raw_line, not trimmed)
        if (raw_line.starts_with("```"))
        {
            if (in_code_fence)
            {
                blocks.push_back(Block{.kind = BlockKind::code_fence, .text = std::move(code_buffer), .language = std::move(code_language)});
                code_buffer.clear();
                code_language.clear();
                in_code_fence = false;
            }
            else
            {
                in_code_fence = true;
                auto lang = raw_line.substr(3);
                while (!lang.empty() && (lang.front() == ' ' || lang.front() == '\t'))
                {
                    lang.remove_prefix(1);
                }
                code_language = std::string{lang};
            }
            continue;
        }

        if (in_code_fence)
        {
            if (!code_buffer.empty())
            {
                code_buffer.push_back('\n');
            }
            code_buffer += raw_line;
            continue;
        }

        // Trim leading whitespace for block detection
        auto line = raw_line;
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
        {
            line.remove_prefix(1);
        }
        line = trim_right(line);

        if (line.empty())
        {
            blocks.push_back(Block{.kind = BlockKind::blank});
            continue;
        }
        if (auto heading = heading_level(line, "### ", BlockKind::heading3))
        {
            blocks.push_back(std::move(*heading));
            continue;
        }
        if (auto heading = heading_level(line, "## ", BlockKind::heading2))
        {
            blocks.push_back(std::move(*heading));
            continue;
        }
        if (auto heading = heading_level(line, "# ", BlockKind::heading1))
        {
            blocks.push_back(std::move(*heading));
            continue;
        }
        if (is_separator_line(line))
        {
            blocks.push_back(Block{.kind = BlockKind::horizontal_rule});
            continue;
        }
        if (line.starts_with("> "))
        {
            blocks.push_back(Block{.kind = BlockKind::quote, .text = std::string{line.substr(2)}});
            continue;
        }
        if (line.starts_with("- ") || line.starts_with("* "))
        {
            blocks.push_back(Block{.kind = BlockKind::bullet, .text = std::string{line.substr(2)}});
            continue;
        }

        // Table detection: if the line has pipes and the next line is a separator
        if (line.find('|') != std::string_view::npos)
        {
            // Reconstruct remaining markdown from the current line
            std::string remaining;
            for (std::size_t i = line_index; i < lines.size(); ++i)
            {
                if (i > line_index)
                {
                    remaining.push_back('\n');
                }
                remaining += lines.at(i);
            }
            auto [table, next_line] = parse_table(remaining, 0);
            if (!table.header.cells.empty())
            {
                // Serialize the table for storage in the Block
                std::ostringstream serialized;
                for (const auto& cell : table.header.cells)
                {
                    serialized << cell.text << '\t';
                }
                serialized << '\n';
                for (const auto& row : table.rows)
                {
                    for (const auto& cell : row.cells)
                    {
                        serialized << cell.text << '\t';
                    }
                    serialized << '\n';
                }
                blocks.push_back(Block{.kind = BlockKind::table, .text = serialized.str()});
                line_index = next_line - 1; // -1 because the loop will ++line_index
                continue;
            }
        }

        blocks.push_back(Block{.kind = BlockKind::paragraph, .text = std::string{line}});
    }

    if (in_code_fence && !code_buffer.empty())
    {
        blocks.push_back(Block{.kind = BlockKind::code_fence, .text = std::move(code_buffer), .language = std::move(code_language)});
    }

    return blocks;
}

auto render_blocks(const std::vector<Block>& blocks, int /*width*/) -> Element
{
    Elements rows;
    for (const auto& block : blocks)
    {
        switch (block.kind)
        {
        case BlockKind::blank:
            rows.push_back(text(" "));
            break;
        case BlockKind::heading1:
            rows.push_back(text(block.text) | bold | color(TuiTheme::primary()));
            rows.push_back(text(std::string(32, '\xe2\x94\x81')) | color(TuiTheme::primary()) | dim);
            break;
        case BlockKind::heading2:
            rows.push_back(text(block.text) | bold);
            break;
        case BlockKind::heading3:
            rows.push_back(text(block.text) | bold | dim);
            break;
        case BlockKind::code_fence:
        {
            Elements code_rows;
            if (!block.language.empty())
            {
                code_rows.push_back(text(block.language) | dim);
            }
            code_rows.push_back(paragraphAlignLeft(block.text) | color(TuiTheme::accent()));
            rows.push_back(vbox(std::move(code_rows)) | borderRounded | color(TuiTheme::text_dim()));
            break;
        }
        case BlockKind::bullet:
            rows.push_back(hbox({text("\xe2\x80\xa2 "), inline_element(block.text)}));
            break;
        case BlockKind::ordered_list:
            rows.push_back(text(block.text));
            break;
        case BlockKind::quote:
            rows.push_back(hbox({text("\xe2\x94\x82 ") | dim, inline_element(block.text) | dim}));
            break;
        case BlockKind::horizontal_rule:
            rows.push_back(text(std::string(48, '\xe2\x94\x80')) | dim);
            break;
        case BlockKind::table:
        {
            // Deserialize the table from block.text
            TableBlock table;
            auto lines = split_lines_view(block.text);
            bool is_header = true;
            for (const auto& line : lines)
            {
                if (line.empty())
                {
                    continue;
                }
                TableRow row;
                std::size_t start = 0;
                while (start <= line.size())
                {
                    auto end = line.find('\t', start);
                    if (end == std::string_view::npos)
                    {
                        auto cell_text = std::string{line.substr(start)};
                        while (!cell_text.empty() && cell_text.back() == '\t')
                        {
                            cell_text.pop_back();
                        }
                        if (!cell_text.empty())
                        {
                            row.cells.push_back(TableCell{.text = cell_text});
                        }
                        break;
                    }
                    auto cell_text = std::string{line.substr(start, end - start)};
                    row.cells.push_back(TableCell{.text = cell_text});
                    start = end + 1;
                }
                if (row.cells.empty())
                {
                    continue;
                }
                if (is_header)
                {
                    table.header = std::move(row);
                    is_header = false;
                }
                else
                {
                    table.rows.push_back(std::move(row));
                }
            }
            if (!table.header.cells.empty())
            {
                rows.push_back(render_table_element(table));
            }
            break;
        }
        case BlockKind::paragraph:
            rows.push_back(inline_element(block.text));
            break;
        }
    }
    return rows.empty() ? text("") : vbox(std::move(rows));
}

auto render_text(std::string_view markdown, int width) -> Element
{
    return render_blocks(parse_blocks(markdown), width);
}

auto render_plain_text(std::string_view markdown, int /*width*/) -> std::string
{
    std::ostringstream output;
    for (const auto& block : parse_blocks(markdown))
    {
        switch (block.kind)
        {
        case BlockKind::blank:
            output << '\n';
            break;
        case BlockKind::heading1:
        case BlockKind::heading2:
        case BlockKind::heading3:
        case BlockKind::paragraph:
        case BlockKind::bullet:
        case BlockKind::quote:
        case BlockKind::ordered_list:
            output << block.text << '\n';
            break;
        case BlockKind::code_fence:
            output << block.text << '\n';
            break;
        case BlockKind::horizontal_rule:
            output << std::string(48, '-') << '\n';
            break;
        case BlockKind::table:
            output << block.text;
            break;
        }
    }
    return output.str();
}

} // namespace codeharness::tui::markdown

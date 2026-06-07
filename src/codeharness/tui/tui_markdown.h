#pragma once

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui::markdown
{

enum class BlockKind
{
    paragraph,
    heading1,
    heading2,
    heading3,
    code_fence,
    bullet,
    ordered_list,
    quote,
    table,
    horizontal_rule,
    blank,
};

struct Block
{
    BlockKind kind = BlockKind::paragraph;
    std::string text;
    std::string language;  // code fence language hint
};

struct TableCell
{
    std::string text;
};

struct TableRow
{
    std::vector<TableCell> cells;
};

struct TableBlock
{
    TableRow header;
    std::vector<TableRow> rows;
    std::vector<int> col_widths;
};

[[nodiscard]] auto parse_blocks(std::string_view markdown) -> std::vector<Block>;
[[nodiscard]] auto render_blocks(const std::vector<Block>& blocks, int width) -> ftxui::Element;
[[nodiscard]] auto render_text(std::string_view markdown, int width) -> ftxui::Element;
[[nodiscard]] auto render_plain_text(std::string_view markdown, int width) -> std::string;

// Table parsing helpers — exposed for testing.
[[nodiscard]] auto parse_table(std::string_view markdown, std::size_t start_line) -> std::pair<TableBlock, std::size_t>;
[[nodiscard]] auto compute_col_widths(const TableBlock& table) -> std::vector<int>;

} // namespace codeharness::tui::markdown

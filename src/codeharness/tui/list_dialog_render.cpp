#include "codeharness/tui/list_dialog_render.h"

#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/selection_list.h"
#include "codeharness/tui/tui_theme.h"

#include <utility>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

auto more_indicator(std::size_t hidden_count) -> std::string
{
    return "\xe2\x96\xbc " + std::to_string(hidden_count) + " more";
}

auto dialog_title(const ListDialogSpec& spec) -> std::string
{
    auto title = spec.title.empty() ? std::string{"Select"} : spec.title;
    if (spec.is_searchable && spec.query.empty())
    {
        title += "  (type to search)";
    }
    return title;
}

auto row_text(const ListDialogRow& row, bool is_selected) -> std::string
{
    auto line = (is_selected ? std::string{k_select_pointer} : std::string{"  "}) + row.primary;
    if (!row.secondary.empty())
    {
        line += "  " + row.secondary;
    }
    if (row.is_current)
    {
        line += std::string{k_current_mark};
    }
    return line;
}

} // namespace

auto list_dialog_hint(bool has_query) -> std::string
{
    return has_query ? "\xe2\x86\x91\xe2\x86\x93 navigate \xc2\xb7 Enter select \xc2\xb7 Esc cancel \xc2\xb7 Backspace clear"
                     : "\xe2\x86\x91\xe2\x86\x93 navigate \xc2\xb7 Enter select \xc2\xb7 Esc cancel";
}

auto render_list_dialog_lines(const ListDialogSpec& spec, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back(horizontal_rule(width));
    lines.push_back(dialog_title(spec));
    lines.push_back(list_dialog_hint(!spec.query.empty()));
    lines.push_back("");
    if (!spec.query.empty())
    {
        lines.push_back("Search: " + spec.query);
    }

    const auto visible = visible_selection_count(spec.rows.size(), spec.page_size);
    if (spec.rows.empty())
    {
        lines.push_back("No matches");
    }
    for (std::size_t row = 0; row < visible; ++row)
    {
        lines.push_back(trim_to_width(row_text(spec.rows.at(row), row == spec.cursor), width));
    }
    if (const auto hidden = hidden_selection_count(spec.rows.size(), spec.page_size); hidden > 0)
    {
        lines.push_back(more_indicator(hidden));
    }

    lines.push_back("");
    lines.push_back(horizontal_rule(width));
    return lines;
}

auto list_dialog_element(const ListDialogSpec& spec, int width) -> Element
{
    Elements rows;
    rows.push_back(text(horizontal_rule(width)) | color(TuiTheme::primary()));
    rows.push_back(text(dialog_title(spec)) | color(TuiTheme::primary()) | bold);
    rows.push_back(text(list_dialog_hint(!spec.query.empty())) | dim);
    rows.push_back(text(" "));

    if (!spec.query.empty())
    {
        rows.push_back(hbox({text("Search: ") | color(TuiTheme::primary()), text(spec.query)}));
    }

    const auto visible = visible_selection_count(spec.rows.size(), spec.page_size);
    if (spec.rows.empty())
    {
        rows.push_back(text("No matches") | dim);
    }
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto& item = spec.rows.at(row);
        const auto is_selected = row == spec.cursor;

        Elements row_parts;
        row_parts.push_back(text(is_selected ? std::string{k_select_pointer} : std::string{"  "}));

        Element primary = text(item.primary);
        if (is_selected)
        {
            primary = primary | color(TuiTheme::primary()) | bold;
        }
        row_parts.push_back(std::move(primary));

        if (!item.secondary.empty())
        {
            row_parts.push_back(text("  " + item.secondary) | dim);
        }
        if (item.is_current)
        {
            row_parts.push_back(text(std::string{k_current_mark}) | color(TuiTheme::success()));
        }

        rows.push_back(hbox(std::move(row_parts)));
    }
    if (const auto hidden = hidden_selection_count(spec.rows.size(), spec.page_size); hidden > 0)
    {
        rows.push_back(text(more_indicator(hidden)) | dim);
    }

    rows.push_back(text(" "));
    rows.push_back(text(horizontal_rule(width)) | color(TuiTheme::primary()));
    return vbox(std::move(rows));
}

} // namespace codeharness::tui::render

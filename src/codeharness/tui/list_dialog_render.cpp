#include "codeharness/tui/list_dialog_render.h"

#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/selection_list.h"
#include "codeharness/tui/tui_theme.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string_view>
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
    if (spec.layout == ListDialogLayout::standard && spec.is_searchable && spec.query.empty())
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

auto option_number(std::size_t row) -> std::string
{
    return std::to_string(row + 1) + ". ";
}

auto model_row_name(const ListDialogRow& row) -> std::string
{
    auto name = row.primary;
    if (row.is_current)
    {
        name += " (current)";
    }
    return name;
}

auto wrap_words(std::string_view text, std::size_t width) -> std::vector<std::string>
{
    if (text.empty())
    {
        return {};
    }

    std::vector<std::string> lines;
    std::istringstream stream{std::string{text}};
    std::string word;
    std::string current;
    while (stream >> word)
    {
        if (current.empty())
        {
            current = std::move(word);
            continue;
        }
        if (current.size() + 1 + word.size() <= width)
        {
            current += " " + word;
        }
        else
        {
            lines.push_back(std::move(current));
            current = std::move(word);
        }
    }
    if (!current.empty())
    {
        lines.push_back(std::move(current));
    }
    return lines;
}

auto model_picker_hint() -> std::string
{
    return "Press enter to confirm or esc to go back";
}

auto render_model_picker_lines(const ListDialogSpec& spec, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back("");
    lines.push_back("  " + (spec.title.empty() ? std::string{"Select Model and Effort"} : spec.title));
    if (spec.is_searchable && !spec.query.empty())
    {
        lines.push_back("  Search: " + spec.query);
    }
    lines.push_back("");

    const auto visible = visible_selection_count(spec.rows.size(), spec.page_size);
    if (spec.rows.empty())
    {
        lines.push_back("  No matches");
    }

    std::size_t name_width = 0;
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto label = option_number(row) + model_row_name(spec.rows.at(row));
        name_width = std::max(name_width, label.size());
    }
    name_width = std::min<std::size_t>(std::max<std::size_t>(name_width, 18), 34);

    const auto available_description_width =
        width > static_cast<int>(name_width + 7) ? static_cast<std::size_t>(width) - name_width - 7 : std::size_t{20};
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto& item = spec.rows.at(row);
        const auto is_selected = row == spec.cursor;
        auto label = option_number(row) + model_row_name(item);
        if (label.size() < name_width)
        {
            label.append(name_width - label.size(), ' ');
        }

        auto description_lines = wrap_words(item.secondary, available_description_width);
        if (description_lines.empty())
        {
            description_lines.push_back("");
        }

        auto line = std::string{is_selected ? k_select_pointer : "  "} + label + "  " + description_lines.front();
        lines.push_back(trim_to_width(std::move(line), width));
        for (std::size_t index = 1; index < description_lines.size(); ++index)
        {
            lines.push_back(trim_to_width(std::string{"  "} + std::string(name_width, ' ') + "  " + description_lines.at(index), width));
        }
    }

    if (const auto hidden = hidden_selection_count(spec.rows.size(), spec.page_size); hidden > 0)
    {
        lines.push_back("  " + more_indicator(hidden));
    }

    lines.push_back("");
    lines.push_back("  " + model_picker_hint());
    return lines;
}

auto model_picker_element(const ListDialogSpec& spec, int width) -> Element
{
    Elements rows;
    rows.push_back(text(" "));
    rows.push_back(text("  " + (spec.title.empty() ? std::string{"Select Model and Effort"} : spec.title)) |
                   color(TuiTheme::text_strong()) | bold);
    if (spec.is_searchable && !spec.query.empty())
    {
        rows.push_back(hbox({text("  Search: ") | color(TuiTheme::primary()), text(spec.query)}));
    }
    rows.push_back(text(" "));

    const auto visible = visible_selection_count(spec.rows.size(), spec.page_size);
    if (spec.rows.empty())
    {
        rows.push_back(text("  No matches") | dim);
    }

    std::size_t name_width = 0;
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto label = option_number(row) + model_row_name(spec.rows.at(row));
        name_width = std::max(name_width, label.size());
    }
    name_width = std::min<std::size_t>(std::max<std::size_t>(name_width, 18), 34);

    const auto available_description_width =
        width > static_cast<int>(name_width + 7) ? static_cast<std::size_t>(width) - name_width - 7 : std::size_t{20};
    for (std::size_t row = 0; row < visible; ++row)
    {
        const auto& item = spec.rows.at(row);
        const auto is_selected = row == spec.cursor;
        auto label = option_number(row) + model_row_name(item);
        if (label.size() < name_width)
        {
            label.append(name_width - label.size(), ' ');
        }

        auto description_lines = wrap_words(item.secondary, available_description_width);
        if (description_lines.empty())
        {
            description_lines.push_back("");
        }

        Element label_element = text(label);
        if (is_selected)
        {
            label_element = label_element | color(TuiTheme::primary()) | bold;
        }

        rows.push_back(hbox({
            text(is_selected ? std::string{k_select_pointer} : std::string{"  "}) |
                (is_selected ? color(TuiTheme::primary()) : color(TuiTheme::text_default())),
            std::move(label_element),
            text("  "),
            text(description_lines.front()) | dim,
        }));
        for (std::size_t index = 1; index < description_lines.size(); ++index)
        {
            rows.push_back(hbox({
                text("  "),
                text(std::string(name_width, ' ')),
                text("  "),
                text(description_lines.at(index)) | dim,
            }));
        }
    }

    if (const auto hidden = hidden_selection_count(spec.rows.size(), spec.page_size); hidden > 0)
    {
        rows.push_back(text("  " + more_indicator(hidden)) | dim);
    }

    rows.push_back(text(" "));
    rows.push_back(text("  " + model_picker_hint()) | dim);
    return vbox(std::move(rows));
}

} // namespace

auto list_dialog_hint(bool has_query) -> std::string
{
    return has_query ? "\xe2\x86\x91\xe2\x86\x93 navigate \xc2\xb7 Enter select \xc2\xb7 Esc cancel \xc2\xb7 Backspace clear"
                     : "\xe2\x86\x91\xe2\x86\x93 navigate \xc2\xb7 Enter select \xc2\xb7 Esc cancel";
}

auto render_list_dialog_lines(const ListDialogSpec& spec, int width) -> std::vector<std::string>
{
    if (spec.layout == ListDialogLayout::model_picker)
    {
        return render_model_picker_lines(spec, width);
    }

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
    if (spec.layout == ListDialogLayout::model_picker)
    {
        return model_picker_element(spec, width);
    }

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

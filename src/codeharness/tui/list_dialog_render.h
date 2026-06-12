#pragma once

#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace codeharness::tui::render
{

struct ListDialogRow
{
    std::string primary;
    std::string secondary;
    bool is_current = false;
};

enum class ListDialogLayout
{
    standard,
    model_picker,
};

struct ListDialogSpec
{
    std::string title;
    std::string query;
    bool is_searchable = false;
    std::size_t cursor = 0;
    std::size_t page_size = 8;
    ListDialogLayout layout = ListDialogLayout::standard;
    std::vector<ListDialogRow> rows;
};

[[nodiscard]] auto list_dialog_hint(bool has_query) -> std::string;
[[nodiscard]] auto render_list_dialog_lines(const ListDialogSpec& spec, int width) -> std::vector<std::string>;
[[nodiscard]] auto list_dialog_element(const ListDialogSpec& spec, int width) -> ftxui::Element;

} // namespace codeharness::tui::render

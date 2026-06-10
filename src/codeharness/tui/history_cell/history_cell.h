#pragma once

#include <string>
#include <string_view>

namespace codeharness::tui
{

enum class HistoryCellKind
{
    user,
    assistant,
    system,
    tool,
    error,
};

enum class ToolStatus
{
    none,
    running,
    completed,
    failed,
};

struct TranscriptItem
{
    HistoryCellKind kind = HistoryCellKind::system;
    std::string text;
    std::string detail;
    std::string id;
    std::string label;
    ToolStatus tool_status = ToolStatus::none;
    bool is_error = false;
    bool expanded = false;
};

[[nodiscard]] auto history_cell_kind_name(HistoryCellKind kind) -> std::string_view;
[[nodiscard]] auto is_expandable_tool_cell(const TranscriptItem& item) -> bool;

} // namespace codeharness::tui

#include "codeharness/tui/history_cell/history_cell.h"

namespace codeharness::tui
{

auto history_cell_kind_name(HistoryCellKind kind) -> std::string_view
{
    switch (kind)
    {
    case HistoryCellKind::user:
        return "user";
    case HistoryCellKind::assistant:
        return "assistant";
    case HistoryCellKind::system:
        return "system";
    case HistoryCellKind::tool:
        return "tool";
    case HistoryCellKind::error:
        return "error";
    }
    return "unknown";
}

auto is_expandable_tool_cell(const TranscriptItem& item) -> bool
{
    return item.kind == HistoryCellKind::tool && !item.detail.empty();
}

} // namespace codeharness::tui

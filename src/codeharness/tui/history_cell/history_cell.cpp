#include "codeharness/tui/history_cell/history_cell.h"

#include <utility>

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

auto make_user_cell(std::string text) -> TranscriptItem
{
    return TranscriptItem{.kind = HistoryCellKind::user, .text = std::move(text)};
}

auto make_assistant_cell(std::string text) -> TranscriptItem
{
    return TranscriptItem{.kind = HistoryCellKind::assistant, .text = std::move(text)};
}

auto make_system_cell(std::string text) -> TranscriptItem
{
    return TranscriptItem{.kind = HistoryCellKind::system, .text = std::move(text)};
}

auto make_error_cell(std::string text) -> TranscriptItem
{
    return TranscriptItem{.kind = HistoryCellKind::error, .text = std::move(text), .is_error = true};
}

auto make_tool_started_cell(std::string id, std::string label) -> TranscriptItem
{
    auto item = TranscriptItem{
        .kind = HistoryCellKind::tool,
        .id = std::move(id),
        .label = std::move(label),
        .tool_status = ToolStatus::running,
        .live = true,
    };
    apply_tool_status(item, ToolStatus::running);
    return item;
}

auto make_tool_finished_cell(std::string id) -> TranscriptItem
{
    return TranscriptItem{
        .kind = HistoryCellKind::tool,
        .text = "completed " + id,
        .id = std::move(id),
        .tool_status = ToolStatus::completed,
    };
}

auto make_tool_result_cell(std::string id, std::string detail, bool is_error) -> TranscriptItem
{
    return TranscriptItem{
        .kind = HistoryCellKind::tool,
        .text = is_error ? "failed" : "completed",
        .detail = std::move(detail),
        .id = std::move(id),
        .tool_status = is_error ? ToolStatus::failed : ToolStatus::completed,
        .is_error = is_error,
        .expanded = is_error,
    };
}

auto append_assistant_delta(TranscriptItem& item, std::string_view delta) -> void
{
    item.text += delta;
}

auto append_tool_input_delta(TranscriptItem& item, std::string_view delta) -> void
{
    item.input_json += delta;
}

auto apply_tool_status(TranscriptItem& item, ToolStatus status, std::string_view detail) -> void
{
    item.tool_status = status;
    if (!detail.empty())
    {
        item.detail = std::string{detail};
    }
    if (item.label.empty())
    {
        return;
    }
    if (status == ToolStatus::running)
    {
        item.text = item.label + " running";
    }
    else if (status == ToolStatus::failed)
    {
        item.text = item.label + " failed";
    }
    else
    {
        item.text = item.label + " completed";
    }
}

} // namespace codeharness::tui

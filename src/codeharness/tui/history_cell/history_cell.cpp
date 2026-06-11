#include "codeharness/tui/history_cell/history_cell.h"

#include <sstream>
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
    return item.kind == HistoryCellKind::tool &&
           (!item.detail.empty() || !item.output_text.empty() || !item.stderr_text.empty());
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
    auto item = TranscriptItem{
        .kind = HistoryCellKind::tool,
        .id = std::move(id),
        .tool_status = ToolStatus::completed,
    };
    apply_tool_status(item, ToolStatus::completed);
    return item;
}

auto make_tool_result_cell(std::string id, std::string detail, bool is_error) -> TranscriptItem
{
    auto item = TranscriptItem{
        .kind = HistoryCellKind::tool,
        .id = std::move(id),
        .tool_status = is_error ? ToolStatus::failed : ToolStatus::completed,
        .is_error = is_error,
        .expanded = is_error,
    };
    apply_tool_status(item, item.tool_status, detail);
    return item;
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
        if (item.is_error || status == ToolStatus::failed)
        {
            item.stderr_text = item.detail;
        }
        else
        {
            item.output_text = item.detail;
        }
    }

    auto label = item.label;
    if (label.empty())
    {
        label = item.id.empty() ? "tool" : item.id;
    }

    if (status == ToolStatus::running)
    {
        item.summary_text = "Running " + label;
    }
    else
    {
        std::ostringstream summary;
        summary << "Ran " << label;
        if (item.is_error || status == ToolStatus::failed)
        {
            summary << " error";
        }
        item.summary_text = summary.str();
    }

    if (!item.duration_label.empty())
    {
        item.summary_text += " " + item.duration_label;
    }

    item.text = item.summary_text;
}

} // namespace codeharness::tui

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
    std::string input_json;
    std::string id;
    std::string label;
    ToolStatus tool_status = ToolStatus::none;
    bool is_error = false;
    bool expanded = false;
    bool live = false;
};

[[nodiscard]] auto history_cell_kind_name(HistoryCellKind kind) -> std::string_view;
[[nodiscard]] auto is_expandable_tool_cell(const TranscriptItem& item) -> bool;
[[nodiscard]] auto make_user_cell(std::string text) -> TranscriptItem;
[[nodiscard]] auto make_assistant_cell(std::string text) -> TranscriptItem;
[[nodiscard]] auto make_system_cell(std::string text) -> TranscriptItem;
[[nodiscard]] auto make_error_cell(std::string text) -> TranscriptItem;
[[nodiscard]] auto make_tool_started_cell(std::string id, std::string label) -> TranscriptItem;
[[nodiscard]] auto make_tool_finished_cell(std::string id) -> TranscriptItem;
[[nodiscard]] auto make_tool_result_cell(std::string id, std::string detail, bool is_error) -> TranscriptItem;
auto append_assistant_delta(TranscriptItem& item, std::string_view delta) -> void;
auto append_tool_input_delta(TranscriptItem& item, std::string_view delta) -> void;
auto apply_tool_status(TranscriptItem& item, ToolStatus status, std::string_view detail = {}) -> void;

} // namespace codeharness::tui

#include "codeharness/tui/chat_surface.h"

#include "codeharness/core/overloaded.h"

#include <algorithm>
#include <utility>

namespace codeharness::tui
{

auto ChatSurface::items() const noexcept -> const std::vector<TranscriptItem>&
{
    return items_;
}

auto ChatSurface::has_streamed_assistant_output() const noexcept -> bool
{
    return streamed_assistant_output_;
}

auto ChatSurface::begin_prompt(std::string prompt) -> void
{
    items_.push_back(make_user_cell(std::move(prompt)));
    streamed_assistant_output_ = false;
}

auto ChatSurface::apply_engine_event(const EngineEvent& event) -> void
{
    std::visit(
        Overloaded{
            [this](const EngineAssistantTextDelta& delta) {
                streamed_assistant_output_ = true;
                if (!items_.empty() && items_.back().kind == HistoryCellKind::assistant)
                {
                    append_assistant_delta(items_.back(), delta.text);
                    return;
                }
                items_.push_back(make_assistant_cell(delta.text));
            },
            [this](const EngineToolStarted& started) {
                if (auto* item = find_tool_item(started.id))
                {
                    item->label = started.name;
                    item->is_error = false;
                    item->expanded = false;
                    apply_tool_status(*item, ToolStatus::running);
                    return;
                }
                items_.push_back(make_tool_started_cell(started.id, started.name));
            },
            [this](const EngineToolFinished& finished) {
                if (auto* item = find_tool_item(finished.id))
                {
                    apply_tool_status(*item, ToolStatus::completed);
                    return;
                }
                items_.push_back(make_tool_finished_cell(finished.id));
            },
            [this](const EngineToolResult& result) {
                if (auto* item = find_tool_item(result.id))
                {
                    item->is_error = result.is_error;
                    item->expanded = result.is_error;
                    apply_tool_status(*item, result.is_error ? ToolStatus::failed : ToolStatus::completed, result.content);
                    return;
                }
                items_.push_back(make_tool_result_cell(result.id, result.content, result.is_error));
            },
            [this](const EngineError& error) {
                append_error_once(error.message);
            },
        },
        event);
}

auto ChatSurface::append_system_message(std::string text) -> void
{
    if (text.empty())
    {
        return;
    }
    items_.push_back(make_system_cell(std::move(text)));
}

auto ChatSurface::append_error_once(std::string text) -> void
{
    if (text.empty() || last_is_error(text))
    {
        return;
    }
    items_.push_back(make_error_cell(std::move(text)));
}

auto ChatSurface::toggle_tool_details(std::size_t transcript_index) -> bool
{
    if (transcript_index >= items_.size())
    {
        return false;
    }

    auto& item = items_.at(transcript_index);
    if (!is_expandable_tool_cell(item))
    {
        return false;
    }

    item.expanded = !item.expanded;
    return true;
}

auto ChatSurface::last_is_error(std::string_view text) const -> bool
{
    return !items_.empty() && items_.back().kind == HistoryCellKind::error && items_.back().text == text;
}

auto ChatSurface::find_tool_item(std::string_view id) -> TranscriptItem*
{
    const auto item = std::ranges::find_if(items_, [id](const TranscriptItem& transcript_item) {
        return transcript_item.kind == HistoryCellKind::tool && transcript_item.id == id;
    });
    if (item == items_.end())
    {
        return nullptr;
    }
    return &*item;
}

} // namespace codeharness::tui

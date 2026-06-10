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

auto ChatSurface::revision() const noexcept -> std::size_t
{
    return revision_;
}

auto ChatSurface::has_streamed_assistant_output() const noexcept -> bool
{
    return streamed_assistant_output_;
}

auto ChatSurface::has_active_response() const noexcept -> bool
{
    return active_response_index_.has_value()
        || std::ranges::any_of(items_, [](const TranscriptItem& item) { return item.live; });
}

auto ChatSurface::begin_prompt(std::string prompt) -> void
{
    finish_active_response();
    items_.push_back(make_user_cell(std::move(prompt)));
    streamed_assistant_output_ = false;
    mark_changed();
}

auto ChatSurface::apply_engine_event(const EngineEvent& event) -> void
{
    std::visit(
        Overloaded{
            [this](const EngineAssistantTextDelta& delta) {
                append_assistant_stream(delta.text);
            },
            [this](const EngineToolStarted& started) {
                finish_assistant_response();
                if (auto* item = find_tool_item(started.id))
                {
                    item->label = started.name;
                    item->is_error = false;
                    item->expanded = false;
                    item->live = true;
                    apply_tool_status(*item, ToolStatus::running);
                    mark_changed();
                    return;
                }
                items_.push_back(make_tool_started_cell(started.id, started.name));
                mark_changed();
            },
            [this](const EngineToolInputDelta& delta) {
                finish_assistant_response();
                if (auto* item = find_tool_item(delta.id))
                {
                    append_tool_input_delta(*item, delta.input_json_delta);
                    mark_changed();
                    return;
                }
                auto item = make_tool_started_cell(delta.id, "tool");
                append_tool_input_delta(item, delta.input_json_delta);
                items_.push_back(std::move(item));
                mark_changed();
            },
            [this](const EngineToolFinished& finished) {
                finish_assistant_response();
                if (auto* item = find_tool_item(finished.id))
                {
                    apply_tool_status(*item, ToolStatus::completed);
                    item->live = false;
                    mark_changed();
                    return;
                }
                items_.push_back(make_tool_finished_cell(finished.id));
                mark_changed();
            },
            [this](const EngineToolResult& result) {
                finish_assistant_response();
                if (auto* item = find_tool_item(result.id))
                {
                    item->is_error = result.is_error;
                    item->expanded = result.is_error;
                    apply_tool_status(*item, result.is_error ? ToolStatus::failed : ToolStatus::completed, result.content);
                    item->live = false;
                    mark_changed();
                    return;
                }
                items_.push_back(make_tool_result_cell(result.id, result.content, result.is_error));
                mark_changed();
            },
            [this](const EngineError& error) {
                append_error_once(error.message);
            },
        },
        event);
}

auto ChatSurface::finish_active_response() -> void
{
    finish_assistant_response();
    bool changed = false;
    for (auto& item : items_)
    {
        if (item.kind == HistoryCellKind::tool && item.live)
        {
            item.live = false;
            changed = true;
        }
    }
    if (changed)
    {
        mark_changed();
    }
}

auto ChatSurface::finish_assistant_response() -> void
{
    if (!active_response_index_)
    {
        return;
    }
    if (*active_response_index_ < items_.size())
    {
        items_.at(*active_response_index_).live = false;
        mark_changed();
    }
    active_response_index_.reset();
}

auto ChatSurface::append_system_message(std::string text) -> void
{
    if (text.empty())
    {
        return;
    }
    finish_active_response();
    items_.push_back(make_system_cell(std::move(text)));
    mark_changed();
}

auto ChatSurface::append_error_once(std::string text) -> void
{
    if (text.empty() || last_is_error(text))
    {
        return;
    }
    finish_active_response();
    items_.push_back(make_error_cell(std::move(text)));
    mark_changed();
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
    mark_changed();
    return true;
}

auto ChatSurface::mark_changed() noexcept -> void
{
    ++revision_;
}

auto ChatSurface::append_assistant_stream(std::string_view delta) -> void
{
    streamed_assistant_output_ = true;
    if (active_response_index_ && *active_response_index_ < items_.size())
    {
        append_assistant_delta(items_.at(*active_response_index_), delta);
        mark_changed();
        return;
    }

    auto item = make_assistant_cell(std::string{delta});
    item.live = true;
    items_.push_back(std::move(item));
    active_response_index_ = items_.size() - 1;
    mark_changed();
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

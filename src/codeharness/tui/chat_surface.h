#pragma once

#include "codeharness/engine/engine.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui
{

enum class ToolStatus
{
    none,
    running,
    completed,
    failed,
};

struct TranscriptItem
{
    std::string kind;
    std::string text;
    std::string detail;
    std::string id;
    std::string label;
    ToolStatus tool_status = ToolStatus::none;
    bool is_error = false;
    bool expanded = false;
};

class ChatSurface
{
public:
    [[nodiscard]] auto items() const noexcept -> const std::vector<TranscriptItem>&;
    [[nodiscard]] auto has_streamed_assistant_output() const noexcept -> bool;

    auto begin_prompt(std::string prompt) -> void;
    auto apply_engine_event(const EngineEvent& event) -> void;
    auto append_system_message(std::string text) -> void;
    auto append_error_once(std::string text) -> void;
    [[nodiscard]] auto toggle_tool_details(std::size_t transcript_index) -> bool;

private:
    [[nodiscard]] auto last_is_error(std::string_view text) const -> bool;
    [[nodiscard]] auto find_tool_item(std::string_view id) -> TranscriptItem*;
    auto update_tool_text(TranscriptItem& item, ToolStatus status, std::string_view detail = {}) -> void;

    std::vector<TranscriptItem> items_;
    bool streamed_assistant_output_ = false;
};

} // namespace codeharness::tui

#pragma once

#include "codeharness/engine/engine.h"
#include "codeharness/tui/history_cell/history_cell.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui
{

class ChatSurface
{
public:
    [[nodiscard]] auto items() const noexcept -> const std::vector<TranscriptItem>&;
    [[nodiscard]] auto revision() const noexcept -> std::size_t;
    [[nodiscard]] auto has_streamed_assistant_output() const noexcept -> bool;
    [[nodiscard]] auto has_active_response() const noexcept -> bool;

    auto begin_prompt(std::string prompt) -> void;
    auto apply_engine_event(const EngineEvent& event) -> void;
    auto finish_active_response() -> void;
    auto append_system_message(std::string text) -> void;
    auto append_error_once(std::string text) -> void;
    [[nodiscard]] auto toggle_tool_details(std::size_t transcript_index) -> bool;

private:
    auto mark_changed() noexcept -> void;
    auto finish_assistant_response() -> void;
    auto append_assistant_stream(std::string_view delta) -> void;
    [[nodiscard]] auto last_is_error(std::string_view text) const -> bool;
    [[nodiscard]] auto find_tool_item(std::string_view id) -> TranscriptItem*;

    std::vector<TranscriptItem> items_;
    std::optional<std::size_t> active_response_index_;
    std::size_t revision_ = 0;
    bool streamed_assistant_output_ = false;
};

} // namespace codeharness::tui

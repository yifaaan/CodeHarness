#pragma once

#include "codeharness/tui/history_cell/history_cell.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui::render
{

constexpr int k_tool_error_max_lines = 8;
constexpr int k_codex_tool_max_lines = 5;
constexpr std::string_view k_transcript_hint = "ctrl + t to view transcript";

[[nodiscard]] auto tool_line_count(std::string_view detail) -> int;
[[nodiscard]] auto tool_summary_text(const TranscriptItem& item) -> std::string;
[[nodiscard]] auto render_history_cell_lines(const TranscriptItem& item, int width) -> std::vector<std::string>;
[[nodiscard]] auto history_cell_element(const TranscriptItem& item, int width) -> ftxui::Element;

} // namespace codeharness::tui::render

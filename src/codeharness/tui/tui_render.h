#pragma once

#include "codeharness/permissions/permission.h"
#include "codeharness/tui/bottom_pane/bottom_pane.h"
#include "codeharness/tui/history_cell/history_cell.h"
#include "codeharness/tui/history_cell/history_cell_render.h"
#include "codeharness/tui/tui_app.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace codeharness::tui::render
{

constexpr int k_command_palette_page_size = 8;
constexpr std::string_view k_separator = " \xe2\x94\x82 "; // │

[[nodiscard]] auto horizontal_rule(int width) -> std::string;
[[nodiscard]] auto format_token_count(int count) -> std::string;

[[nodiscard]] auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>;
[[nodiscard]] auto render_transcript_lines(const std::vector<TranscriptItem>& transcript, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_command_palette_lines(const CommandPaletteState& palette, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_select_modal_lines(const SelectModalState& modal, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_permission_lines(const PermissionPrompt& prompt, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_status_footer_line(const TuiDisplayConfig& config, const TuiState& state) -> std::string;
[[nodiscard]] auto render_composer_hint(bool busy, int history_index = -1) -> std::string;

[[nodiscard]] auto welcome_banner_element(const TuiDisplayConfig& config) -> ftxui::Element;
[[nodiscard]] auto transcript_item_element(const TranscriptItem& item, int width) -> ftxui::Element;
[[nodiscard]] auto command_palette_element(const CommandPaletteState& palette, int width) -> ftxui::Element;
[[nodiscard]] auto permission_modal_element(const PermissionPrompt& prompt, int width) -> ftxui::Element;
[[nodiscard]] auto select_modal_element(const SelectModalState& modal, int width) -> ftxui::Element;
[[nodiscard]] auto question_modal_element(const QuestionModalState& modal, int width) -> ftxui::Element;
[[nodiscard]] auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> ftxui::Element;
[[nodiscard]] auto busy_spinner_frame(int frame) -> std::string;

} // namespace codeharness::tui::render

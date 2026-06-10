#pragma once

#include "codeharness/permissions/permission.h"
#include "codeharness/tui/bottom_pane/bottom_pane.h"
#include "codeharness/tui/history_cell/history_cell.h"
#include "codeharness/tui/history_cell/history_cell_render.h"
#include "codeharness/tui/status_footer_render.h"
#include "codeharness/tui/tui_app.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace codeharness::tui::render
{

constexpr int k_command_palette_page_size = 8;

[[nodiscard]] auto horizontal_rule(int width) -> std::string;

[[nodiscard]] auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>;
[[nodiscard]] auto render_transcript_lines(const std::vector<TranscriptItem>& transcript, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_command_palette_lines(const CommandPaletteState& palette, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_select_modal_lines(const SelectModalState& modal, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_permission_lines(const PermissionPrompt& prompt, int width) -> std::vector<std::string>;
[[nodiscard]] auto render_question_lines(const QuestionModalState& modal, int width) -> std::vector<std::string>;

[[nodiscard]] auto welcome_banner_element(const TuiDisplayConfig& config) -> ftxui::Element;
[[nodiscard]] auto transcript_item_element(const TranscriptItem& item, int width) -> ftxui::Element;
[[nodiscard]] auto command_palette_element(const CommandPaletteState& palette, int width) -> ftxui::Element;
[[nodiscard]] auto permission_modal_element(const PermissionPrompt& prompt, int width) -> ftxui::Element;
[[nodiscard]] auto select_modal_element(const SelectModalState& modal, int width) -> ftxui::Element;
[[nodiscard]] auto question_modal_element(const QuestionModalState& modal, int width) -> ftxui::Element;

} // namespace codeharness::tui::render

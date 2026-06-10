#pragma once

#include "codeharness/tui/tui_app.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>

namespace codeharness::tui::render
{

constexpr std::string_view k_separator = " \xe2\x94\x82 ";

[[nodiscard]] auto format_token_count(int count) -> std::string;
[[nodiscard]] auto render_status_footer_line(const TuiDisplayConfig& config, const TuiState& state) -> std::string;
[[nodiscard]] auto render_composer_hint(bool busy, int history_index = -1) -> std::string;
[[nodiscard]] auto busy_spinner_frame(int frame) -> std::string;
[[nodiscard]] auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> ftxui::Element;

} // namespace codeharness::tui::render

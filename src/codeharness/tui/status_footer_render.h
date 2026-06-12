#pragma once

#include "codeharness/tui/tui_app.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>

namespace codeharness::tui::render
{

/// Separator string used in status footer (│).
constexpr std::string_view k_separator = " \xe2\x94\x82 ";

/// Format a token count for display (e.g., 1.5k for 1500).
[[nodiscard]] auto format_token_count(int count) -> std::string;

/// Render the status footer line as plain text.
[[nodiscard]] auto render_status_footer_line(const TuiDisplayConfig& config, const TuiState& state) -> std::string;

/// Render the composer hint line.
/// Shows different hints based on whether the agent is busy.
[[nodiscard]] auto render_composer_hint(bool busy, int history_index = -1) -> std::string;

/// Get a legacy spinner frame by index.
[[nodiscard]] auto busy_spinner_frame(int frame) -> std::string;

/// True when the working status should be visible.
[[nodiscard]] auto should_animate_working_status(const TuiState& state) -> bool;

/// Render the status footer as a ftxui element.
[[nodiscard]] auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> ftxui::Element;

/// Render a working status indicator with elapsed time.
/// Shows Codex-style bullet + shimmered "Working" + elapsed time + interrupt hint.
[[nodiscard]] auto working_status_element(int elapsed_seconds, const std::string& header, int animation_frame = 0) -> ftxui::Element;

/// Format elapsed seconds into compact human-friendly form.
/// Matches codex-cli fmt_elapsed_compact: 0s, 59s, 1m 00s, 59m 59s, 1h 00m 00s
[[nodiscard]] auto fmt_elapsed_compact(int elapsed_seconds) -> std::string;

/// Render the complete status footer with working indicator when busy.
[[nodiscard]] auto full_status_footer_element(const TuiDisplayConfig& config,
                                              const TuiState& state,
                                              int elapsed_seconds,
                                              int animation_frame = 0) -> ftxui::Element;

/// Render a combined Codex-style single-line footer.
/// Left side: hint text (e.g., "? for shortcuts").
/// Right side: status line (model, directory, mode) via status_footer_element().
/// Uses filler() to right-align the status content.
[[nodiscard]] auto codex_footer_element(const TuiDisplayConfig& config,
                                        const TuiState& state,
                                        const std::string& hint) -> ftxui::Element;

} // namespace codeharness::tui::render

#pragma once

#include <string>

namespace codeharness::tui::render
{

[[nodiscard]] auto trim_to_width(std::string text, int width) -> std::string;
[[nodiscard]] auto horizontal_rule(int width) -> std::string;

} // namespace codeharness::tui::render

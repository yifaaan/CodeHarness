#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

/// Simple diff renderer that shows +/- lines with color.
class DiffRenderer
{
public:
	/// Render a unified-diff string to colored FTXUI elements.
	static ftxui::Element Render(const std::string& diff);
};

} // namespace codeharness::tui
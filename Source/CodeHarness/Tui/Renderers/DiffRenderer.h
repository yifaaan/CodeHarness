#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

	/// Renders a unified-diff string with conventional +/-/@@ coloring.
	/// Lines starting with `+` (but not `++`) are green, `-` (but not `--`) are
	/// red, `@@` hunk headers are cyan, and `\ No newline at end of file` is
	/// shown in a muted color. Diff metadata lines (`diff `, `index `, `---`,
	/// `+++`, `@@`) are dimmed. Anything else falls back to plain text.
	class DiffRenderer
	{
	public:
		static ftxui::Element Render(const std::string& diff);
	};

} // namespace codeharness::tui

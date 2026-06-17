#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

	struct TuiState;

	/// Inline panel showing extended-thinking output (`state->currentThinking`)
	/// as a dimmed, indented block. Renders nothing when there is no thinking
	/// text. Designed to appear in the transcript stream itself.
	class ThinkingView
	{
	public:
		static ftxui::Element Render(const std::string& text, bool expanded = false);
	};

} // namespace codeharness::tui

#pragma once

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

	/// FTXUI component for the chat message transcript area.
	/// Renders the transcript entries from TuiState as a scrollable list.
	class ChatPane
	{
	public:
		static ftxui::Component Create();
	};

} // namespace codeharness::tui
#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

	struct TuiState;

	/// Renders `state->todos` as a checklist. Empty state shows "No active tasks".
	/// Hidden entirely when `state->todoPanelVisible` is false.
	class TodoPanel
	{
	public:
		static ftxui::Component Create(std::shared_ptr<TuiState> state);
	};

} // namespace codeharness::tui

#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

	struct TuiState;

	/// Bottom status bar component showing model, mode, streaming status, and usage.
	class StatusBar
	{
	public:
		static ftxui::Component Create(std::shared_ptr<TuiState> state);
	};

} // namespace codeharness::tui

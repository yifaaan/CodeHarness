#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

	struct TuiState;

	class WelcomePanel
	{
	public:
		static ftxui::Component Create(std::shared_ptr<TuiState> state);
	};

} // namespace codeharness::tui

#include "Tui/Utils/KeyBindings.h"

#include <ftxui/component/event.hpp>

namespace codeharness::tui
{

	/*static*/ std::optional<KeyBindings::Action> KeyBindings::MapEvent(const ftxui::Event& event)
	{
		if (event == ftxui::Event::Return)
			return Action::Submit;
		if (event == ftxui::Event::CtrlC)
			return Action::Cancel;
		if (event == ftxui::Event::Escape)
			return Action::Exit;
		if (event == ftxui::Event::ArrowUp)
			return Action::Up;
		if (event == ftxui::Event::ArrowDown)
			return Action::Down;
		return std::nullopt;
	}

	/*static*/ std::string KeyBindings::Describe(Action action)
	{
		switch (action)
		{
		case Action::Submit:
			return "Enter: Send message";
		case Action::Cancel:
			return "Ctrl+C: Cancel streaming";
		case Action::NewSession:
			return "Ctrl+N: New session";
		case Action::ClearContext:
			return "Ctrl+L: Clear context";
		case Action::Help:
			return "F1: Show help";
		case Action::Exit:
			return "Escape: Close dialog / Exit";
		case Action::Up:
			return "Up: Previous input";
		case Action::Down:
			return "Down: Next input";
		}
		return "";
	}

} // namespace codeharness::tui
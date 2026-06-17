#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace ftxui
{
	class Event;
}

namespace codeharness::tui
{

	/// Defines and describes keyboard shortcuts.
	class KeyBindings
	{
	public:
		enum class Action
		{
			Submit,
			Cancel,
			NewSession,
			ClearContext,
			Help,
			Exit,
			Up,
			Down,
		};

		static std::optional<Action> MapEvent(const ftxui::Event& event);
		static std::string Describe(Action action);
	};

} // namespace codeharness::tui
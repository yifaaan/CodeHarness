#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>

#include "Session/SessionTypes.h"

namespace codeharness::tui
{

	enum class SessionPickerScope
	{
		Cwd,
		All,
	};

	struct SessionPickerOptions
	{
		std::function<std::vector<session::SessionInfo>()> sessions;
		std::function<std::string()> currentSessionId;
		std::function<SessionPickerScope()> scope;
		std::function<void(session::SessionInfo)> onSelect;
		std::function<void()> onCancel;
		std::function<void()> onToggleScope;
		std::function<void()> onCtrlC;
		std::function<void()> onCtrlD;
	};

	class SessionPicker
	{
	public:
		static ftxui::Component Create(SessionPickerOptions options);
	};

} // namespace codeharness::tui

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>

#include "Tui/Utils/SlashCommands.h"

namespace codeharness::tui
{

	struct HelpDialogOptions
	{
		std::vector<SlashCommands::Command> commands;
		std::function<void()> onClose;
		int maxVisible = 24;
	};

	class HelpDialog
	{
	public:
		static ftxui::Component Create(HelpDialogOptions options);
	};

} // namespace codeharness::tui

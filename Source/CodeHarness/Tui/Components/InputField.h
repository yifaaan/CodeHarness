#pragma once

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

/// FTXUI component for the text input area at the bottom of the chat.
/// Supports history navigation, slash commands, and submit.
class InputField
{
public:
	static ftxui::Component Create();
};

} // namespace codeharness::tui
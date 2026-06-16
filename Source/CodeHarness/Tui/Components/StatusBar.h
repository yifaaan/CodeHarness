#pragma once

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

/// Bottom status bar component showing model, mode, streaming status, and usage.
class StatusBar
{
public:
	static ftxui::Component Create();
};

} // namespace codeharness::tui
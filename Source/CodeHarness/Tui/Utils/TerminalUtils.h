#pragma once

#include <string>

namespace codeharness::tui
{

/// Utility functions for terminal interaction.
class TerminalUtils
{
public:
	/// Get terminal width in characters.
	static int GetWidth();

	/// Get terminal height in characters.
	static int GetHeight();
};

} // namespace codeharness::tui
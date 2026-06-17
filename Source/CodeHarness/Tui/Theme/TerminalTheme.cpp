#include "Tui/Theme/TerminalTheme.h"

#include "Tui/TuiState.h"

namespace codeharness::tui
{

	bool TerminalTheme::IsDark()
	{
		return DetectDarkMode();
	}

} // namespace codeharness::tui
#pragma once

#include "Tui/TuiState.h"

namespace codeharness::tui
{

/// Theme management.
class Theme
{
public:
	static ColorPalette Detect();
	static ColorPalette MakePalette(bool darkMode);
};

} // namespace codeharness::tui
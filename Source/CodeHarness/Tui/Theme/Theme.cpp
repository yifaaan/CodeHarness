#include "Tui/Theme/Theme.h"

#include "Tui/TuiState.h"

namespace codeharness::tui
{

ColorPalette Theme::Detect()
{
	return MakePalette(DetectDarkMode());
}

ColorPalette Theme::MakePalette(bool darkMode)
{
	return tui::MakePalette(darkMode);
}

} // namespace codeharness::tui
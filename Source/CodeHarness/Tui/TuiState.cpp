#include "Tui/TuiState.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace codeharness::tui
{

	void ApplyToolOutputExpanded(TuiState& state, bool expanded)
	{
		state.toolOutputExpanded = expanded;
		for (auto& [_, tool] : state.activeToolCalls)
		{
			tool.expanded = expanded;
		}
		for (auto& [_, tool] : state.completedToolCalls)
		{
			tool.expanded = expanded;
		}
	}

	bool DetectDarkMode()
	{
#ifdef _WIN32
		// Windows: query registry for personalization setting
		HKEY hKey;
		DWORD value = 0;
		DWORD size = sizeof(value);
		if (RegOpenKeyExW(HKEY_CURRENT_USER,
						  L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
						  0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
							 reinterpret_cast<LPBYTE>(&value), &size);
			RegCloseKey(hKey);
			// value == 0 means dark, value == 1 means light
			return value == 0;
		}
#endif
		// Default to dark
		return true;
	}

	ColorPalette MakePalette(bool darkMode)
	{
		if (darkMode)
		{
			return {
				.fg = 15,		// white
				.bg = 0,		// black
				.accent = 4,	// blue
				.success = 2,	// green
				.error = 1,		// red
				.warning = 3,	// yellow
				.muted = 8,		// gray
				.highlight = 6, // cyan
			};
		}
		else
		{
			return {
				.fg = 0,		 // black
				.bg = 15,		 // white
				.accent = 12,	 // bright blue
				.success = 10,	 // bright green
				.error = 9,		 // bright red
				.warning = 11,	 // bright yellow
				.muted = 7,		 // light gray
				.highlight = 14, // bright cyan
			};
		}
	}

} // namespace codeharness::tui

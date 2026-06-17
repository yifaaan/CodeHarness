#include "Tui/Utils/TerminalUtils.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace codeharness::tui
{

	int TerminalUtils::GetWidth()
	{
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
		{
			return csbi.srWindow.Right - csbi.srWindow.Left + 1;
		}
#endif
		return 80;
	}

	int TerminalUtils::GetHeight()
	{
#ifdef _WIN32
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
		{
			return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
		}
#endif
		return 24;
	}

} // namespace codeharness::tui
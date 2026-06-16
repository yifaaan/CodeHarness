#pragma once

namespace codeharness::tui
{

/// Detect terminal theme (dark/light) via OSC 11 query.
class TerminalTheme
{
public:
	static bool IsDark();
};

} // namespace codeharness::tui
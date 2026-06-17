#pragma once

#include <unknwn.h>

#include "Views.ShellPage.g.h"

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	struct ShellPage : ShellPageT<ShellPage>
	{
		ShellPage();
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation

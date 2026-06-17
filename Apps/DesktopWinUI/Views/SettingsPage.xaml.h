#pragma once

#include <unknwn.h>

#include "Views.SettingsPage.g.h"

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	struct SettingsPage : SettingsPageT<SettingsPage>
	{
		SettingsPage();
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct SettingsPage : SettingsPageT<SettingsPage, implementation::SettingsPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation

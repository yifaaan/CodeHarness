#pragma once

#include <unknwn.h>

#include "Controls.Sidebar.g.h"

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct Sidebar : SidebarT<Sidebar>
	{
		Sidebar();
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct Sidebar : SidebarT<Sidebar, implementation::Sidebar>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

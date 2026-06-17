#pragma once

#include "Controls.ToolCallView.g.h"

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct ToolCallView : ToolCallViewT<ToolCallView>
	{
		ToolCallView();
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct ToolCallView : ToolCallViewT<ToolCallView, implementation::ToolCallView>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

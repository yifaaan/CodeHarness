#pragma once

#include "Controls.ComposerBox.g.h"

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct ComposerBox : ComposerBoxT<ComposerBox>
	{
		ComposerBox();
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct ComposerBox : ComposerBoxT<ComposerBox, implementation::ComposerBox>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

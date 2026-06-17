#pragma once

#include "Controls.PermissionDialog.g.h"

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct PermissionDialog : PermissionDialogT<PermissionDialog>
	{
		PermissionDialog();
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct PermissionDialog : PermissionDialogT<PermissionDialog, implementation::PermissionDialog>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

#pragma once

#include <vector>

#include "Desktop/DesktopModels.h"

namespace codeharness::desktop_app
{

	struct SessionListViewModel
	{
		std::vector<desktop::DesktopSessionItem> sessions;
	};

} // namespace codeharness::desktop_app

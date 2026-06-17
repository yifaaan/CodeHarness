#pragma once

#include <string>

namespace codeharness::desktop_app
{

	struct MessageViewModel
	{
		std::string text;
		bool assistant = false;
	};

} // namespace codeharness::desktop_app

#pragma once

#include "Views.ChatPage.g.h"

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	struct ChatPage : ChatPageT<ChatPage>
	{
		ChatPage();
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct ChatPage : ChatPageT<ChatPage, implementation::ChatPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation

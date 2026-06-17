#pragma once

#include <functional>

#include <winrt/Microsoft.UI.Dispatching.h>

namespace codeharness::desktop_app
{

	class UiDispatcher
	{
	public:
		explicit UiDispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue);
		void Enqueue(std::function<void()> callback) const;

	private:
		winrt::Microsoft::UI::Dispatching::DispatcherQueue queue{nullptr};
	};

} // namespace codeharness::desktop_app

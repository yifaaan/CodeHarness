#include "Services/UiDispatcher.h"

#include <utility>

namespace codeharness::desktop_app
{

	UiDispatcher::UiDispatcher(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue) : queue(queue)
	{
	}

	void UiDispatcher::Enqueue(std::function<void()> callback) const
	{
		queue.TryEnqueue([callback = std::move(callback)]() { callback(); });
	}

} // namespace codeharness::desktop_app

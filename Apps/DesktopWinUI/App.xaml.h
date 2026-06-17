#pragma once

#include "App.g.h"
#include "MainWindow.xaml.h"

namespace winrt::CodeHarness::Desktop::implementation
{

	struct App : AppT<App>
	{
		App();
		void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

	private:
		CodeHarness::Desktop::MainWindow window{nullptr};
	};

} // namespace winrt::CodeHarness::Desktop::implementation

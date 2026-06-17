#include "App.xaml.h"

#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::CodeHarness::Desktop::implementation
{

	App::App()
	{
	}

	void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
	{
		window = make<MainWindow>();
		window.Activate();
	}

} // namespace winrt::CodeHarness::Desktop::implementation

#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::CodeHarness::Desktop::implementation
{

	App::App()
	{
		this->InitializeComponent();
	}

	void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
	{
		window = make<MainWindow>();
		window.as<implementation::MainWindow>()->ApplyDefaultWindowSize();
		window.Activate();
	}

} // namespace winrt::CodeHarness::Desktop::implementation

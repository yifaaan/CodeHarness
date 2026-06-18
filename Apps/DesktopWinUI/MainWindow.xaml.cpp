#include "MainWindow.xaml.h"
#include "Views/ShellPage.xaml.h"

#include <Windows.h>
#include <microsoft.ui.xaml.window.h>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::CodeHarness::Desktop::implementation
{

	MainWindow::MainWindow()
	{
		this->InitializeComponent();
		InitializeShell();
	}

	void MainWindow::ApplyDefaultWindowSize()
	{
		// Called from App::OnLaunched where the projected Microsoft::UI::Xaml::Window
		// base is directly available; HWND is exposed via the raw COM IWindowNative.
		auto baseWindow = (*this).try_as<winrt::Microsoft::UI::Xaml::Window>();
		if (!baseWindow)
		{
			return;
		}
		winrt::com_ptr<::IWindowNative> windowNative;
		baseWindow.as(windowNative);
		if (windowNative)
		{
			HWND hwnd = nullptr;
			if (SUCCEEDED(windowNative->get_WindowHandle(&hwnd)) && hwnd)
			{
				SetWindowPos(hwnd, nullptr, 120, 90, 1280, 820, SWP_NOZORDER);
			}
		}
	}

	void MainWindow::InitializeShell()
	{
		// The ShellPage owns the 3-column layout, DesktopCoreService, and all
		// event wiring. Initializing it here (rather than in the XAML ctor) keeps
		// the window's XamlRoot stable for any dialogs the shell shows.
		auto shell = this->Shell().try_as<winrt::CodeHarness::Desktop::Views::implementation::ShellPage>();
		if (shell)
		{
			shell->Initialize();
		}
	}

} // namespace winrt::CodeHarness::Desktop::implementation

#pragma once

#include <unknwn.h>

// Projected types for the XAML-hosted child views. The generated MainWindow.g.h
// references winrt::CodeHarness::Desktop::Views::ShellPage, so its full projection
// must be visible BEFORE MainWindow.g.h is included.
#include <winrt/CodeHarness.Desktop.Views.h>

#include "MainWindow.g.h"

namespace winrt::CodeHarness::Desktop::implementation
{

	struct MainWindow : MainWindowT<MainWindow>
	{
		MainWindow();

		// Position/size the window for the 3-column layout (HWND-only API).
		void ApplyDefaultWindowSize();

	private:
		void InitializeShell();
	};

} // namespace winrt::CodeHarness::Desktop::implementation

namespace winrt::CodeHarness::Desktop::factory_implementation
{

	struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
	{
	};

} // namespace winrt::CodeHarness::Desktop::factory_implementation

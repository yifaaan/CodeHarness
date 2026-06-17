#pragma once

#include <unknwn.h>

#include "MainWindow.g.h"

#include <memory>
#include <string>

#include "Services/DesktopCoreService.h"

namespace winrt::CodeHarness::Desktop::implementation
{

	struct MainWindow : MainWindowT<MainWindow>
	{
		MainWindow();
		~MainWindow();

	private:
		void InitializeUi();
		void LoadSessions();
		void NewChat();
		void ResumeSelectedSession();
		void SendPrompt();
		void CancelPrompt();
		void AppendMessage(std::wstring const& text, bool subtle = false);
		void AppendAssistantDelta(std::wstring const& text);
		void SetRunning(bool running);
		void SetEmptyState(bool empty);
		void ShowStatus(std::wstring const& text);
		std::wstring ToWide(std::string_view text) const;
		std::string ToUtf8(hstring const& text) const;

		std::unique_ptr<::codeharness::desktop_app::DesktopCoreService> core;
		std::wstring currentAssistantText;
		bool running = false;
	};

} // namespace winrt::CodeHarness::Desktop::implementation

namespace winrt::CodeHarness::Desktop::factory_implementation
{

	struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
	{
	};

} // namespace winrt::CodeHarness::Desktop::factory_implementation

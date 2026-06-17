#pragma once

#include <unknwn.h>

#include "MainWindow.g.h"

#include <memory>
#include <string>

#include "Services/DesktopCoreService.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::CodeHarness::Desktop::implementation
{

	struct MainWindow : MainWindowT<MainWindow>
	{
		MainWindow();
		~MainWindow();

		// Position/size the window for the 3-column layout (HWND-only API).
		void ApplyDefaultWindowSize();

	private:
		void InitializeUi();
		void LoadSessions();
		void FillPlaceholderGroups();
		Microsoft::UI::Xaml::Controls::ListViewItem BuildSessionRow(const codeharness::desktop::DesktopSessionItem& session);
		Microsoft::UI::Xaml::Controls::Border BuildGhostRow(std::wstring const& title);
		void ApplySessionItemTitleStyle(Microsoft::UI::Xaml::Controls::TextBlock const& block);
		void ApplySessionItemMetaStyle(Microsoft::UI::Xaml::Controls::TextBlock const& block);
		std::wstring FormatRelativeTime(std::int64_t updatedAtMs);
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

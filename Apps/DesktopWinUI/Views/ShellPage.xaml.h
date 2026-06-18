#pragma once

#include <unknwn.h>

// Projected types for the XAML-hosted child controls (Sidebar / ChatPage). The
// generated ShellPage.g.h references winrt::CodeHarness::Desktop::Controls::Sidebar
// and ::Views::ChatPage, so their full projections must be visible BEFORE the
// generated ShellPage.g.h is included.
#include <winrt/CodeHarness.Desktop.Controls.h>
#include <winrt/CodeHarness.Desktop.Views.h>

#include "Views.ShellPage.g.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Services/DesktopCoreService.h"

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	// One row of the right-column ACTIVITY list.
	struct ToolActivityEntry
	{
		std::wstring name;
		std::wstring status;
		bool isError = false;
	};

	struct ShellPage : ShellPageT<ShellPage>
	{
		ShellPage();

		// Called by MainWindow once the page is hosted so XamlRoot is ready.
		void Initialize();
		void FocusComposer();

	private:
		void WireCoreCallbacks();
		void WireSidebarCallbacks();
		void WireChatCallbacks();
		void LoadSessions();
		void SendText(std::wstring text);
		void NewChat();
		void ResumeSession(std::wstring titleHint);
		void CancelPrompt();
		void OpenSettings();

		// Right-column Context panel refreshers (pure UI, driven by accumulated state).
		void RefreshUsage();
		void RefreshActivity();
		void RefreshOpenFiles();

		void CollectFilePath(nlohmann::json const& args);

		static std::wstring ToWide(std::string_view text);
		static std::wstring FormatTokenCount(std::int64_t tokens);
		static std::wstring BaseName(std::wstring const& path);
		static std::wstring ExtensionLabel(std::wstring const& path);

		std::unique_ptr<::codeharness::desktop_app::DesktopCoreService> m_core;
		bool m_running = false;

		// Context panel accumulated state. All updated from the event callback and
		// reset on NewChat(). The 128k denominator is a hardcoded stand-in until
		// the core exposes per-model maxContextTokens.
		std::int64_t m_totalTokens = 0;          // cumulative across turns
		int m_currentSteps = 0;                  // live step counter for the active turn
		std::vector<ToolActivityEntry> m_activity;   // most-recent-first, capped at 20
		std::vector<std::wstring> m_openFiles;        // deduped, most-recent-first
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation

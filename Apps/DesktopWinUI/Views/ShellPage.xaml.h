#pragma once

#include <unknwn.h>

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

	// One file change entry in the Git panel.
	struct GitChangeEntry
	{
		std::wstring path;
		wchar_t status = L'M'; // M = modified, A = added, D = deleted, R = renamed, C = copied
		std::int64_t additions = 0;
		std::int64_t deletions = 0;
	};

	struct ShellPage : ShellPageT<ShellPage>
	{
		ShellPage();

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

		// Git panel refreshers.
		void RefreshGitChanges();
		void RefreshBranchInfo();
		void RefreshUsage();
		void RefreshProgress();

		// Populate the sidebar workdir, chat title/branch pills, and the Git panel
		// branch from the live backend (workdir, git rev-parse).
		void RefreshEnvironment();

		void CollectGitChange(nlohmann::json const& args);

		static std::wstring ToWide(std::string_view text);
		static std::wstring FormatTokenCount(std::int64_t tokens);

		std::unique_ptr<::codeharness::desktop_app::DesktopCoreService> m_core;
		bool m_running = false;

		// Token / step counters (the right-side usage panel was removed, but
		// the counters are still maintained internally for future use).
		std::int64_t m_totalTokens = 0;
		std::int64_t m_currentSteps = 0;

		// Accumulated reasoning text for the in-flight thinking block. Flushed to
		// ChatPage::AppendThinkingBlock when the assistant's visible text starts
		// (first AssistantDelta) or the turn ends.
		std::wstring m_currentThinking;

		// Completed tool-call labels for the right-panel Progress section.
		// most-recent-first, capped at 50.
		std::vector<std::wstring> m_completedSteps;

		// Git panel state.  Updated from the event callback and reset on NewChat().
		std::wstring m_currentBranch = L"main";
		std::vector<GitChangeEntry> m_gitChanges; // most-recent-first, capped at 50
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct ShellPage : ShellPageT<ShellPage, implementation::ShellPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation
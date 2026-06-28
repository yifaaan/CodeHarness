#pragma once

#include <unknwn.h>

#include <winrt/CodeHarness.Desktop.Controls.h>
#include <winrt/CodeHarness.Desktop.Views.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include "Views.ShellPage.g.h"

#include <cstdint>
#include <memory>
#include <set>
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
		void OnTopSessionMenuClick(winrt::Windows::Foundation::IInspectable const& sender,
		                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnTopGitClick(winrt::Windows::Foundation::IInspectable const& sender,
		                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnTopFilesClick(winrt::Windows::Foundation::IInspectable const& sender,
		                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnSidebarToggleClick(winrt::Windows::Foundation::IInspectable const& sender,
		                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnWorkspaceToggleClick(winrt::Windows::Foundation::IInspectable const& sender,
		                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnWorkspaceGitTabClick(winrt::Windows::Foundation::IInspectable const& sender,
		                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnWorkspaceProgressTabClick(winrt::Windows::Foundation::IInspectable const& sender,
		                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnWorkspaceFilesTabClick(winrt::Windows::Foundation::IInspectable const& sender,
		                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesNewFileClick(winrt::Windows::Foundation::IInspectable const& sender,
		                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesNewFolderClick(winrt::Windows::Foundation::IInspectable const& sender,
		                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesCreateItemClick(winrt::Windows::Foundation::IInspectable const& sender,
		                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesCancelNewItemClick(winrt::Windows::Foundation::IInspectable const& sender,
		                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesNewItemKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
		                           winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
		void OnFilesRefreshClick(winrt::Windows::Foundation::IInspectable const& sender,
		                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnFilesSearchTextChanged(winrt::Windows::Foundation::IInspectable const& sender,
		                              winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
		void OnSidebarGutterPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
		                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnSidebarGutterPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
		                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnSidebarGutterPointerReleased(winrt::Windows::Foundation::IInspectable const& sender,
		                                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnSidebarGutterPointerCanceled(winrt::Windows::Foundation::IInspectable const& sender,
		                                    winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnSidebarGutterDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
		                                 winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
		void OnWorkspaceGutterPointerPressed(winrt::Windows::Foundation::IInspectable const& sender,
		                                     winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnWorkspaceGutterPointerMoved(winrt::Windows::Foundation::IInspectable const& sender,
		                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnWorkspaceGutterPointerReleased(winrt::Windows::Foundation::IInspectable const& sender,
		                                      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnWorkspaceGutterPointerCanceled(winrt::Windows::Foundation::IInspectable const& sender,
		                                      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
		void OnWorkspaceGutterDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
		                                   winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
		void OnSettingsClick(winrt::Windows::Foundation::IInspectable const& sender,
		                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

	private:
		void WireCoreCallbacks();
		void WireSidebarCallbacks();
		void WireChatCallbacks();
		void LoadSessions();
		void SendText(std::wstring text);
		void NewChat();
		void ResumeSession(std::wstring titleHint);
		void AddSessionToSplit(std::wstring sessionId, std::wstring titleHint);
		void RenameSession(std::wstring sessionId, std::wstring title);
		void PromptRenameSession(std::wstring sessionId);
		void CopySessionId(std::wstring const& sessionId);
		void ConfirmDeleteSession(std::wstring sessionId);
		void CancelPrompt();
		void OpenSettings();
		void SetWorkspaceVisible(bool visible);
		void SetWorkspaceTab(std::int32_t tab);
		void FinishSidebarResize();
		void FinishWorkspaceResize();
		void BeginFilesNewItem(bool folder);
		void CreateFilesNewItem();
		void CancelFilesNewItem();
		void ToggleFileDirectory(std::wstring relative);
		bool IsFileDirectoryExpanded(std::wstring const& relative) const;
		std::wstring FilesCreateTargetBreadcrumb() const;

		// Git panel refreshers.
		void RefreshGitChanges();
		void RefreshBranchInfo();
		void RefreshUsage();
		void RefreshProgress();
		void RefreshFiles();

		// Populate the sidebar workdir, chat title/branch pills, and the Git panel
		// branch from the live backend (workdir, git rev-parse).
		void RefreshEnvironment();

		void CollectGitChange(nlohmann::json const& args);
		std::wstring FindSessionTitle(std::wstring const& sessionId);

		static std::wstring ToWide(std::string_view text);
		static std::wstring FormatTokenCount(std::int64_t tokens);

		std::unique_ptr<::codeharness::desktop_app::DesktopCoreService> m_core;
		bool m_running = false;
		bool m_sidebarVisible = true;
		bool m_workspaceVisible = true;
		std::int32_t m_workspaceTab = 0;
		double m_sidebarWidth = 240.0;
		double m_workspaceWidth = 320.0;
		double m_resizeStartX = 0.0;
		bool m_resizingSidebar = false;
		bool m_resizingWorkspace = false;
		bool m_creatingFolder = false;
		std::set<std::wstring> m_expandedFileDirectories;
		std::wstring m_selectedFilesFolder;
		std::wstring m_selectedFilesPath;
		std::wstring m_activeSessionId;

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

#pragma once

#include <unknwn.h>

#include "Controls.Sidebar.g.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct Sidebar : SidebarT<Sidebar>
	{
		Sidebar();

		// IDL-projected surface.
		void SetSessions(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessions);
		void SetSessionsWithTimestamps(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> titles,
		                               winrt::Windows::Foundation::Collections::IVectorView<std::int64_t> createdAtMs);
		void SetSessionItems(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> titles,
		                     winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessionIds,
		                     winrt::Windows::Foundation::Collections::IVectorView<std::int64_t> createdAtMs);
		void SetActiveSession(winrt::hstring title);
		void SetWorkdir(winrt::hstring workdir);
		void Focus();
		void AddSplitSession(std::wstring sessionId, std::wstring title);

		// C++-only callbacks (sibling XAML pages set these directly).
		void OnNewChat(std::function<void()> cb) { m_onNewChat = std::move(cb); }
		void OnResume(std::function<void(std::wstring)> cb) { m_onResume = std::move(cb); }
		void OnAddToSplit(std::function<void(std::wstring, std::wstring)> cb) { m_onAddToSplit = std::move(cb); }
		void OnFork(std::function<void(std::wstring)> cb) { m_onFork = std::move(cb); }
		void OnRename(std::function<void(std::wstring, std::wstring)> cb) { m_onRename = std::move(cb); }
		void OnDelete(std::function<void(std::wstring)> cb) { m_onDelete = std::move(cb); }
		void OnOpenSettings(std::function<void()> cb) { m_onOpenSettings = std::move(cb); }

		// XAML-wired handlers.
		void OnNewChatClick(winrt::Windows::Foundation::IInspectable const& sender,
		                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnSessionDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
		                           winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
		void OnSettingsClick(winrt::Windows::Foundation::IInspectable const& sender,
		                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnSearchClick(winrt::Windows::Foundation::IInspectable const& sender,
		                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
		void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const& sender,
		                         winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
		void OnSearchClose(winrt::Windows::Foundation::IInspectable const& sender,
		                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

	private:
		// Re-render the session list from m_allSessions, optionally filtered by the
		// current search query (case-insensitive substring match on title).
		void RebuildSessionList();
		void RebuildSplitGroup();
		void SyncSplitSessionsWithSessionList();
		void RemoveSplitSession(std::wstring sessionId);
		bool IsInSplitGroup(std::wstring const& sessionId) const;

		// One cached session entry (mirrors what SetSessionsWithTimestamps receives).
		struct SessionEntry
		{
			std::wstring id;
			std::wstring title;
			std::int64_t createdAtMs = 0;
		};

		struct SplitEntry
		{
			std::wstring id;
			std::wstring title;
		};

		std::vector<SessionEntry> m_allSessions;
		std::vector<SplitEntry> m_splitSessions;
		std::wstring m_activeTitle;
		bool m_searchVisible = false;

		std::function<void()> m_onNewChat;
		std::function<void(std::wstring)> m_onResume;
		std::function<void(std::wstring, std::wstring)> m_onAddToSplit;
		std::function<void(std::wstring)> m_onFork;
		std::function<void(std::wstring, std::wstring)> m_onRename;
		std::function<void(std::wstring)> m_onDelete;
		std::function<void()> m_onOpenSettings;
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct Sidebar : SidebarT<Sidebar, implementation::Sidebar>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

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
		void SetWorkdir(winrt::hstring workdir);
		void Focus();

		// C++-only callbacks (sibling XAML pages set these directly).
		void OnNewChat(std::function<void()> cb) { m_onNewChat = std::move(cb); }
		void OnResume(std::function<void(std::wstring)> cb) { m_onResume = std::move(cb); }
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

		// One cached session entry (mirrors what SetSessionsWithTimestamps receives).
		struct SessionEntry
		{
			std::wstring title;
			std::int64_t createdAtMs = 0;
		};

		std::vector<SessionEntry> m_allSessions;
		bool m_searchVisible = false;

		std::function<void()> m_onNewChat;
		std::function<void(std::wstring)> m_onResume;
		std::function<void()> m_onOpenSettings;
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct Sidebar : SidebarT<Sidebar, implementation::Sidebar>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

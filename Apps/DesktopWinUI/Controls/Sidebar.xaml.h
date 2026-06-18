#pragma once

#include <unknwn.h>

#include "Controls.Sidebar.g.h"

#include <functional>
#include <string>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct Sidebar : SidebarT<Sidebar>
	{
		Sidebar();

		// IDL-projected surface.
		void SetSessions(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessions);
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

	private:
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

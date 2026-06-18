#pragma once

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Controls.Sidebar.g.h"
#include "Desktop/DesktopModels.h"

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	struct Sidebar : SidebarT<Sidebar>
	{
		Sidebar();

		void LoadSessions(std::vector<codeharness::desktop::DesktopSessionItem> const& sessions);
		void SetWorkdir(std::wstring const& path);

		void SetNewChatCallback(std::function<void()> callback);
		void SetResumeCallback(std::function<void(std::string sessionId)> callback);
		void SetSearchCallback(std::function<void(std::wstring query)> callback);
		void SetSettingsCallback(std::function<void()> callback);

	private:
		void OnNewChat();
		void OnSearchToggle();
		void OnSearchClose();
		void OnSearchTextChanged();
		void OnOpenSettings();

		void BuildSessionGroups();
		void PopulateGroup(Microsoft::UI::Xaml::Controls::StackPanel panel,
						   std::vector<codeharness::desktop::DesktopSessionItem> const& sessions);
		Microsoft::UI::Xaml::Controls::Border BuildSessionRow(codeharness::desktop::DesktopSessionItem const& session);
		std::wstring FormatRelativeTime(std::int64_t updatedAtMs);
		static std::wstring ToWide(std::string_view text);

		std::vector<codeharness::desktop::DesktopSessionItem> allSessions;

		std::function<void()> newChatCallback;
		std::function<void(std::string)> resumeCallback;
		std::function<void(std::wstring)> searchCallback;
		std::function<void()> settingsCallback;

		bool searchVisible = false;
	};

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

namespace winrt::CodeHarness::Desktop::Controls::factory_implementation
{

	struct Sidebar : SidebarT<Sidebar, implementation::Sidebar>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Controls::factory_implementation

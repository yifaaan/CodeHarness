#include "Controls/Sidebar.xaml.h"

#include <chrono>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/base.h>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	Sidebar::Sidebar()
	{
		this->InitializeComponent();

		this->NewChatBtn().Click([this](auto&&, auto&&) { OnNewChat(); });
		this->SearchBtn().Click([this](auto&&, auto&&) { OnSearchToggle(); });
		this->SearchCloseBtn().Click([this](auto&&, auto&&) { OnSearchClose(); });
		this->SearchBox().TextChanged([this](auto&&, auto&&) { OnSearchTextChanged(); });
		this->SettingsBtn().Click([this](auto&&, auto&&) { OnOpenSettings(); });
	}

	void Sidebar::LoadSessions(std::vector<codeharness::desktop::DesktopSessionItem> const& sessions)
	{
		allSessions = sessions;
		BuildSessionGroups();
	}

	void Sidebar::SetWorkdir(std::wstring const& path)
	{
		this->WorkdirText().Text(path);
	}

	void Sidebar::SetNewChatCallback(std::function<void()> callback)
	{
		newChatCallback = std::move(callback);
	}

	void Sidebar::SetResumeCallback(std::function<void(std::string sessionId)> callback)
	{
		resumeCallback = std::move(callback);
	}

	void Sidebar::SetSearchCallback(std::function<void(std::wstring query)> callback)
	{
		searchCallback = std::move(callback);
	}

	void Sidebar::SetSettingsCallback(std::function<void()> callback)
	{
		settingsCallback = std::move(callback);
	}

	void Sidebar::OnNewChat()
	{
		if (newChatCallback)
		{
			newChatCallback();
		}
	}

	void Sidebar::OnSearchToggle()
	{
		searchVisible = !searchVisible;
		this->SearchBtn().Visibility(searchVisible ? Visibility::Collapsed : Visibility::Visible);
		this->SearchBoxPanel().Visibility(searchVisible ? Visibility::Visible : Visibility::Collapsed);
		if (searchVisible)
		{
			this->SearchBox().Focus(FocusState::Programmatic);
		}
		else
		{
			this->SearchBox().Text(L"");
		}
	}

	void Sidebar::OnSearchClose()
	{
		OnSearchToggle();
	}

	void Sidebar::OnSearchTextChanged()
	{
		std::wstring query(this->SearchBox().Text());
		if (searchCallback)
		{
			searchCallback(query);
		}
	}

	void Sidebar::OnOpenSettings()
	{
		if (settingsCallback)
		{
			settingsCallback();
		}
	}

	void Sidebar::BuildSessionGroups()
	{
		// Clear all groups
		auto clearGroup = [](StackPanel panel) {
			if (panel)
			{
				panel.Children().Clear();
			}
		};
		clearGroup(this->TodayGroup());
		clearGroup(this->WeekGroup());
		clearGroup(this->MonthGroup());
		clearGroup(this->EarlierGroup());

		// Group sessions by recency
		const auto now = std::chrono::system_clock::now();

		std::vector<codeharness::desktop::DesktopSessionItem> today, week, month, earlier;

		for (const auto& session : allSessions)
		{
			if (session.updatedAt <= 0)
			{
				today.push_back(session);
				continue;
			}
			const auto updated = std::chrono::system_clock::from_time_t(session.updatedAt / 1000);
			const auto diffHours = std::chrono::duration_cast<std::chrono::hours>(now - updated).count();

			if (diffHours < 24)
			{
				today.push_back(session);
			}
			else if (diffHours < 24 * 7)
			{
				week.push_back(session);
			}
			else if (diffHours < 24 * 30)
			{
				month.push_back(session);
			}
			else
			{
				earlier.push_back(session);
			}
		}

		auto showGroup = [](TextBlock header, StackPanel group, bool hasItems) {
			auto vis = hasItems ? Visibility::Visible : Visibility::Collapsed;
			if (header)
			{
				header.Visibility(vis);
			}
			if (group)
			{
				group.Visibility(vis);
			}
		};

		showGroup(this->TodayHeader(), this->TodayGroup(), !today.empty());
		showGroup(this->WeekHeader(), this->WeekGroup(), !week.empty());
		showGroup(this->MonthHeader(), this->MonthGroup(), !month.empty());
		showGroup(this->EarlierHeader(), this->EarlierGroup(), !earlier.empty());

		PopulateGroup(this->TodayGroup(), today);
		PopulateGroup(this->WeekGroup(), week);
		PopulateGroup(this->MonthGroup(), month);
		PopulateGroup(this->EarlierGroup(), earlier);
	}

	void Sidebar::PopulateGroup(StackPanel panel,
								std::vector<codeharness::desktop::DesktopSessionItem> const& sessions)
	{
		if (!panel)
		{
			return;
		}
		for (const auto& session : sessions)
		{
			panel.Children().Append(BuildSessionRow(session));
		}
	}

	Border Sidebar::BuildSessionRow(codeharness::desktop::DesktopSessionItem const& session)
	{
		const auto title = session.title.empty() ? session.sessionId : session.title;

		Border row;
		row.Padding(Thickness{14, 7, 14, 7});
		row.CornerRadius(CornerRadiusHelper::FromUniformRadius(8.0));
		row.Margin(Thickness{4, 1, 4, 1});

		// Double-tap resumes the session
		const auto sessionId = session.sessionId;
		row.DoubleTapped([this, sessionId](auto&&, auto&&) {
			if (resumeCallback)
			{
				resumeCallback(sessionId);
			}
		});

		StackPanel panel;
		panel.Spacing(2);

		TextBlock titleBlock;
		titleBlock.Text(ToWide(title));
		titleBlock.FontSize(13);
		titleBlock.Foreground(SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		titleBlock.TextTrimming(TextTrimming::CharacterEllipsis);

		TextBlock metaBlock;
		metaBlock.Text(FormatRelativeTime(session.updatedAt));
		metaBlock.FontSize(11);
		metaBlock.Foreground(SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		metaBlock.TextTrimming(TextTrimming::CharacterEllipsis);

		panel.Children().Append(titleBlock);
		panel.Children().Append(metaBlock);
		row.Child(panel);
		return row;
	}

	std::wstring Sidebar::FormatRelativeTime(std::int64_t updatedAtMs)
	{
		if (updatedAtMs <= 0)
		{
			return L"new";
		}
		const auto now = std::chrono::system_clock::now();
		const auto updated = std::chrono::system_clock::from_time_t(updatedAtMs / 1000);
		const auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - updated);
		const auto minutes = diff.count() / 60;
		if (minutes < 1)
		{
			return L"just now";
		}
		if (minutes < 60)
		{
			return std::to_wstring(minutes) + L"m ago";
		}
		const auto hours = minutes / 60;
		if (hours < 24)
		{
			return std::to_wstring(hours) + L"h ago";
		}
		const auto days = hours / 24;
		return std::to_wstring(days) + L"d ago";
	}

	std::wstring Sidebar::ToWide(std::string_view text)
	{
		auto value = winrt::to_hstring(std::string{text});
		return std::wstring{value.c_str(), value.size()};
	}

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

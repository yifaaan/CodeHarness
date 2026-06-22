#include "Controls/Sidebar.xaml.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <winrt/base.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	namespace
	{
		void ApplySessionItemTitleStyle(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block)
		{
			block.FontSize(13);
			block.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 31, 31, 30}));
			block.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
		}

		void ApplySessionItemTimestampStyle(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block)
		{
			block.FontSize(11);
			block.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 138, 138, 134}));
		}

		winrt::Microsoft::UI::Xaml::Controls::Border MakeSessionItem(
			std::wstring const& title,
			std::wstring const& timestamp,
			std::function<void(std::wstring)> const& onDoubleTap)
		{
			winrt::Microsoft::UI::Xaml::Controls::Border border;
			border.Padding(winrt::Microsoft::UI::Xaml::Thickness{14, 7, 14, 7});
			border.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{8});
			border.Margin(winrt::Microsoft::UI::Xaml::Thickness{4, 1, 4, 1});

			winrt::Microsoft::UI::Xaml::Controls::Grid grid;
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition titleCol;
				titleCol.Width(winrt::Microsoft::UI::Xaml::GridLength{1, winrt::Microsoft::UI::Xaml::GridUnitType::Star});
				grid.ColumnDefinitions().Append(titleCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition timeCol;
				timeCol.Width(winrt::Microsoft::UI::Xaml::GridLength{0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto});
				grid.ColumnDefinitions().Append(timeCol);
			}
			grid.ColumnSpacing(8);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock titleBlock;
			titleBlock.Text(winrt::hstring{title.c_str(), static_cast<uint32_t>(title.size())});
			ApplySessionItemTitleStyle(titleBlock);
			titleBlock.Opacity(0.85);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(titleBlock, 0);
			grid.Children().Append(titleBlock);

			if (!timestamp.empty())
			{
				winrt::Microsoft::UI::Xaml::Controls::TextBlock timeBlock;
				timeBlock.Text(winrt::hstring{timestamp.c_str(), static_cast<uint32_t>(timestamp.size())});
				ApplySessionItemTimestampStyle(timeBlock);
				timeBlock.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
				winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(timeBlock, 1);
				grid.Children().Append(timeBlock);
			}

			border.Child(grid);

			// Bind DoubleTapped via lambda capture (routed event bubbles up to Sidebar).
			if (onDoubleTap)
			{
				std::wstring capturedTitle = title;
				border.DoubleTapped([onDoubleTap, capturedTitle](
					winrt::Windows::Foundation::IInspectable const&,
					winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
				{
					onDoubleTap(capturedTitle);
				});
			}

			return border;
		}

		// Time grouping thresholds (milliseconds).
		constexpr std::int64_t kOneDayMs = 86400000;
		constexpr std::int64_t kSevenDaysMs = 7 * kOneDayMs;
		constexpr std::int64_t kThirtyDaysMs = 30 * kOneDayMs;

		enum class TimeGroup { Today, Week, Month, Earlier };

		TimeGroup ClassifyByTimestamp(std::int64_t createdAtMs, std::int64_t nowMs)
		{
			auto diffMs = nowMs - createdAtMs;
			if (diffMs < 0) return TimeGroup::Today;
			if (diffMs < kOneDayMs) return TimeGroup::Today;
			if (diffMs < kSevenDaysMs) return TimeGroup::Week;
			if (diffMs < kThirtyDaysMs) return TimeGroup::Month;
			return TimeGroup::Earlier;
		}

		void ClearAllGroups(
			winrt::Microsoft::UI::Xaml::Controls::StackPanel const& g1,
			winrt::Microsoft::UI::Xaml::Controls::StackPanel const& g2,
			winrt::Microsoft::UI::Xaml::Controls::StackPanel const& g3,
			winrt::Microsoft::UI::Xaml::Controls::StackPanel const& g4)
		{
			g1.Children().Clear();
			g2.Children().Clear();
			g3.Children().Clear();
			g4.Children().Clear();
		}

		void UpdateGroupHeaders(
			winrt::Microsoft::UI::Xaml::Controls::TextBlock const& h1,
			winrt::Microsoft::UI::Xaml::Controls::TextBlock const& h2,
			winrt::Microsoft::UI::Xaml::Controls::TextBlock const& h3,
			winrt::Microsoft::UI::Xaml::Controls::TextBlock const& h4,
			bool has1, bool has2, bool has3, bool has4)
		{
			auto V = winrt::Microsoft::UI::Xaml::Visibility::Visible;
			auto C = winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
			h1.Visibility(has1 ? V : C);
			h2.Visibility(has2 ? V : C);
			h3.Visibility(has3 ? V : C);
			h4.Visibility(has4 ? V : C);
		}
	} // namespace

	Sidebar::Sidebar()
	{
		this->InitializeComponent();
	}

	void Sidebar::SetSessions(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessions)
	{
		// No timestamps provided: cache the titles with "now" as createdAt so they
		// all land in the Today group, then render.
		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		m_allSessions.clear();
		for (auto const& title : sessions)
		{
			m_allSessions.push_back({ std::wstring{title.c_str(), title.size()}, nowMs });
		}
		RebuildSessionList();
	}

	void Sidebar::SetSessionsWithTimestamps(
		winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> titles,
		winrt::Windows::Foundation::Collections::IVectorView<std::int64_t> createdAtMs)
	{
		m_allSessions.clear();
		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		auto count = titles.Size();
		for (uint32_t i = 0; i < count; ++i)
		{
			std::wstring t{titles.GetAt(i).c_str(), titles.GetAt(i).size()};
			std::int64_t ts = (i < createdAtMs.Size()) ? createdAtMs.GetAt(i) : nowMs;
			m_allSessions.push_back({ std::move(t), ts });
		}
		RebuildSessionList();
	}

	void Sidebar::RebuildSessionList()
	{
		auto todayGroup = this->TodayGroup();
		auto weekGroup = this->WeekGroup();
		auto monthGroup = this->MonthGroup();
		auto earlierGroup = this->EarlierGroup();
		ClearAllGroups(todayGroup, weekGroup, monthGroup, earlierGroup);

		// Active search query (empty when the search box is hidden/cleared).
		std::wstring query;
		if (m_searchVisible)
		{
			auto boxText = this->SearchBox().Text();
			query = std::wstring{boxText.c_str(), boxText.size()};
		}
		// Case-insensitive substring match helper.
		auto matches = [&query](std::wstring const& title) {
			if (query.empty()) return true;
			if (title.size() < query.size()) return false;
			auto it = std::search(title.begin(), title.end(), query.begin(), query.end(),
				[](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
			return it != title.end();
		};

		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

		bool hasToday = false, hasWeek = false, hasMonth = false, hasEarlier = false;

		for (auto const& entry : m_allSessions)
		{
			if (!matches(entry.title)) continue;
			auto group = ClassifyByTimestamp(entry.createdAtMs, nowMs);

			// Relative timestamp label: show concrete elapsed time ("刚刚"/"3小时前"/"2天前")
			// rather than broad bucket names, to match the reference layout.
			std::wstring tsStr;
			auto diffMs = nowMs - entry.createdAtMs;
			if (diffMs < 0) diffMs = 0;
			auto diffMins = diffMs / 60000;
			if (diffMins < 1) tsStr = L"刚刚";
			else if (diffMins < 60) tsStr = std::to_wstring(diffMins) + L"分钟前";
			else if (diffMins < 60 * 24) tsStr = std::to_wstring(diffMins / 60) + L"小时前";
			else
			{
				auto days = diffMins / (60 * 24);
				if (days < 30) tsStr = std::to_wstring(days) + L"天前";
				else tsStr = std::to_wstring(days / 30) + L"个月前";
			}

			auto item = MakeSessionItem(entry.title, tsStr, m_onResume);
			switch (group)
			{
			case TimeGroup::Today:   todayGroup.Children().Append(item); hasToday = true; break;
			case TimeGroup::Week:    weekGroup.Children().Append(item);  hasWeek = true;  break;
			case TimeGroup::Month:   monthGroup.Children().Append(item); hasMonth = true; break;
			case TimeGroup::Earlier: earlierGroup.Children().Append(item); hasEarlier = true; break;
			}
		}

		UpdateGroupHeaders(
			this->TodayHeader(), this->WeekHeader(), this->MonthHeader(), this->EarlierHeader(),
			hasToday, hasWeek, hasMonth, hasEarlier);
	}

	void Sidebar::SetWorkdir(winrt::hstring workdir)
	{
		this->WorkdirText().Text(workdir);
	}

	void Sidebar::Focus()
	{
		this->NewChatBtn().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
	}

	void Sidebar::OnNewChatClick(winrt::Windows::Foundation::IInspectable const&,
	                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (m_onNewChat)
		{
			m_onNewChat();
		}
	}

	void Sidebar::OnSessionDoubleTapped(winrt::Windows::Foundation::IInspectable const& sender,
	                                    winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
	{
		// Fallback handler for XAML-wired items (none currently, but kept for safety).
		auto border = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>();
		if (!border) return;
		auto grid = border.Child().try_as<winrt::Microsoft::UI::Xaml::Controls::Grid>();
		if (!grid) return;
		auto children = grid.Children();
		if (children.Size() == 0) return;
		auto textBlock = children.GetAt(0).try_as<winrt::Microsoft::UI::Xaml::Controls::TextBlock>();
		if (!textBlock) return;
		auto text = textBlock.Text();
		std::wstring value{text.c_str(), text.size()};
		if (m_onResume)
		{
			m_onResume(std::move(value));
		}
	}

	void Sidebar::OnSettingsClick(winrt::Windows::Foundation::IInspectable const&,
	                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (m_onOpenSettings)
		{
			m_onOpenSettings();
		}
	}

	void Sidebar::OnSearchClick(winrt::Windows::Foundation::IInspectable const&,
	                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		// Toggle the search input panel.
		m_searchVisible = !m_searchVisible;
		auto panel = this->SearchBoxPanel();
		panel.Visibility(m_searchVisible ? winrt::Microsoft::UI::Xaml::Visibility::Visible
		                                 : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
		if (m_searchVisible)
		{
			this->SearchBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
		}
		else
		{
			this->SearchBox().Text(L"");
		}
		RebuildSessionList();
	}

	void Sidebar::OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const&,
	                                  winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
	{
		if (m_searchVisible)
		{
			RebuildSessionList();
		}
	}

	void Sidebar::OnSearchClose(winrt::Windows::Foundation::IInspectable const&,
	                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		m_searchVisible = false;
		this->SearchBoxPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
		this->SearchBox().Text(L"");
		RebuildSessionList();
	}

} // namespace winrt::CodeHarness::Desktop::Controls::implementation
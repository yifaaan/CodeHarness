#include "Controls/Sidebar.xaml.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string_view>
#include <winrt/base.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	namespace
	{
		void ApplySessionItemTitleStyle(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block)
		{
			block.FontSize(13);
			block.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 36, 36, 36}));
			block.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
		}

		void ApplySessionItemTimestampStyle(winrt::Microsoft::UI::Xaml::Controls::TextBlock const& block)
		{
			block.FontSize(11);
			block.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 133, 133, 125}));
		}

		winrt::Microsoft::UI::Xaml::Controls::Border MakeSessionItem(
			std::wstring const& sessionId,
			std::wstring const& title,
			std::wstring const& timestamp,
			bool isActive,
			bool canAddToSplit,
			std::function<void(std::wstring)> const& onResume,
			std::function<void(std::wstring, std::wstring)> const& onAddToSplit,
			std::function<void(std::wstring)> const& onFork,
			std::function<void(std::wstring, std::wstring)> const& onRename,
			std::function<void(std::wstring)> const& onDelete)
		{
			winrt::Microsoft::UI::Xaml::Controls::Border border;
			border.Padding(winrt::Microsoft::UI::Xaml::Thickness{8, 5, 8, 5});
			border.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{12, 12, 12, 12});
			border.Margin(winrt::Microsoft::UI::Xaml::Thickness{4, 1, 4, 1});
			border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
				isActive ? winrt::Windows::UI::Color{255, 236, 236, 232}
				         : winrt::Windows::UI::Color{0, 255, 255, 255}));
			border.MinHeight(32);

			winrt::Microsoft::UI::Xaml::Controls::Grid grid;
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition markerCol;
				markerCol.Width(winrt::Microsoft::UI::Xaml::GridLength{18, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
				grid.ColumnDefinitions().Append(markerCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition titleCol;
				titleCol.Width(winrt::Microsoft::UI::Xaml::GridLength{1, winrt::Microsoft::UI::Xaml::GridUnitType::Star});
				grid.ColumnDefinitions().Append(titleCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition timeCol;
				timeCol.Width(winrt::Microsoft::UI::Xaml::GridLength{38, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
				grid.ColumnDefinitions().Append(timeCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition actionCol;
				actionCol.Width(winrt::Microsoft::UI::Xaml::GridLength{22, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
				grid.ColumnDefinitions().Append(actionCol);
			}
			grid.ColumnSpacing(4);

			winrt::Microsoft::UI::Xaml::Controls::Border marker;
			marker.Width(4);
			marker.Height(4);
			marker.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{2, 2, 2, 2});
			marker.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
				isActive ? winrt::Windows::UI::Color{255, 36, 36, 36}
				         : winrt::Windows::UI::Color{255, 196, 196, 188}));
			marker.Opacity(isActive ? 0.75 : 0.55);
			marker.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			marker.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(marker, 0);
			grid.Children().Append(marker);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock titleBlock;
			titleBlock.Text(winrt::hstring{title.c_str(), static_cast<uint32_t>(title.size())});
			ApplySessionItemTitleStyle(titleBlock);
			titleBlock.Opacity(isActive ? 1.0 : 0.85);
			if (isActive)
			{
				titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeight{500});
			}
			titleBlock.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(titleBlock, 1);
			grid.Children().Append(titleBlock);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock timeBlock;
			if (!timestamp.empty())
			{
				timeBlock.Text(winrt::hstring{timestamp.c_str(), static_cast<uint32_t>(timestamp.size())});
				ApplySessionItemTimestampStyle(timeBlock);
				timeBlock.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
				timeBlock.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Right);
				timeBlock.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
				winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(timeBlock, 2);
				grid.Children().Append(timeBlock);
			}

			winrt::Microsoft::UI::Xaml::Controls::Button actionButton;
			actionButton.Content(winrt::box_value(L"\uE712"));
			actionButton.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
			actionButton.FontSize(12);
			actionButton.Width(22);
			actionButton.Height(22);
			actionButton.Padding(winrt::Microsoft::UI::Xaml::Thickness{0, 0, 0, 0});
			actionButton.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 255, 255, 255}));
			actionButton.BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{0});
			actionButton.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 133, 133, 125}));
			actionButton.Opacity(0);
			actionButton.IsHitTestVisible(false);
			actionButton.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(actionButton, 3);
			actionButton.Click([sessionId, title, canAddToSplit, onAddToSplit, onFork, onRename, onDelete, actionButton](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
			{
				winrt::Microsoft::UI::Xaml::Controls::MenuFlyout flyout;

				auto appendItem = [&](std::wstring_view label, std::wstring_view glyph, auto handler) {
					winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem item;
					item.Text(winrt::hstring{ label });
					winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
					icon.Glyph(winrt::hstring{ glyph });
					item.Icon(icon);
					item.Click(handler);
					flyout.Items().Append(item);
					return item;
				};

				auto splitItem = appendItem(L"\u5206\u5C4F", L"\uE8A7", [sessionId, title, onAddToSplit](auto&&, auto&&) {
					if (!sessionId.empty() && onAddToSplit)
					{
						onAddToSplit(sessionId, title);
					}
				});
				splitItem.IsEnabled(canAddToSplit && static_cast<bool>(onAddToSplit));

				auto forkItem = appendItem(L"分叉会话", L"\uE8A7", [sessionId, onFork](auto&&, auto&&) {
					if (!sessionId.empty() && onFork)
					{
						onFork(sessionId);
					}
				});
				forkItem.IsEnabled(!sessionId.empty() && static_cast<bool>(onFork));

				appendItem(L"重命名", L"\uE70F", [sessionId, title, onRename, actionButton](auto&&, auto&&) {
					if (sessionId.empty() || !onRename)
					{
						return;
					}

					winrt::Microsoft::UI::Xaml::Controls::StackPanel panel;
					panel.Spacing(8);
					winrt::Microsoft::UI::Xaml::Controls::TextBlock hint;
					hint.Text(L"为当前会话输入一个新标题。");
					hint.FontSize(12);
					hint.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 133, 133, 125}));
					panel.Children().Append(hint);

					winrt::Microsoft::UI::Xaml::Controls::TextBox input;
					input.Text(winrt::hstring{ title });
					input.MinWidth(320);
					input.SelectAll();
					panel.Children().Append(input);

					winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
					dialog.XamlRoot(actionButton.XamlRoot());
					dialog.Title(winrt::box_value(L"重命名会话"));
					dialog.Content(panel);
					dialog.PrimaryButtonText(L"保存");
					dialog.CloseButtonText(L"取消");
					dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
					auto op = dialog.ShowAsync();
					op.Completed([sessionId, input, onRename](auto const& async, auto) {
						if (async.GetResults() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
						{
							return;
						}
						auto value = input.Text();
						auto nextTitle = std::wstring{ value.c_str(), value.size() };
						if (!nextTitle.empty())
						{
							onRename(sessionId, nextTitle);
						}
					});
				});

				auto copyIdItem = appendItem(L"复制会话 ID", L"\uE8C8", [sessionId](auto&&, auto&&) {
					winrt::Windows::ApplicationModel::DataTransfer::DataPackage data;
					data.SetText(winrt::hstring{ sessionId });
					winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
				});
				copyIdItem.IsEnabled(!sessionId.empty());

				flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});

				auto deleteItem = appendItem(L"删除会话", L"\uE74D", [sessionId, onDelete](auto&&, auto&&) {
					if (!sessionId.empty() && onDelete)
					{
						onDelete(sessionId);
					}
				});
				deleteItem.IsEnabled(!sessionId.empty() && static_cast<bool>(onDelete));
				deleteItem.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 178, 54, 54}));

				flyout.ShowAt(actionButton);
			});
			actionButton.Tapped([](winrt::Windows::Foundation::IInspectable const&,
			                       winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
			{
				args.Handled(true);
			});
			grid.Children().Append(actionButton);

			border.Child(grid);

			border.PointerEntered([border, timeBlock, actionButton, isActive](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				if (!isActive)
				{
					border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 245, 245, 244}));
				}
				if (timeBlock)
				{
					timeBlock.Opacity(0);
				}
				actionButton.Opacity(1);
				actionButton.IsHitTestVisible(true);
			});
			border.PointerExited([border, timeBlock, actionButton, isActive](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
					isActive ? winrt::Windows::UI::Color{255, 236, 236, 232}
					         : winrt::Windows::UI::Color{0, 255, 255, 255}));
				if (timeBlock)
				{
					timeBlock.Opacity(1);
				}
				actionButton.Opacity(0);
				actionButton.IsHitTestVisible(false);
			});

			// Single-click mirrors CodePilot's conversation list behavior.
			if (onResume)
			{
				auto resumeKey = sessionId.empty() ? title : sessionId;
				border.Tapped([onResume, resumeKey = std::move(resumeKey)](
					winrt::Windows::Foundation::IInspectable const&,
					winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
				{
					args.Handled(true);
					onResume(resumeKey);
				});
			}

			return border;
		}

		winrt::Microsoft::UI::Xaml::Controls::Border MakeSplitItem(
			std::wstring const& sessionId,
			std::wstring const& title,
			bool isActive,
			std::function<void(std::wstring)> const& onSelect,
			std::function<void(std::wstring)> const& onRemove)
		{
			winrt::Microsoft::UI::Xaml::Controls::Border border;
			border.Padding(winrt::Microsoft::UI::Xaml::Thickness{8, 6, 6, 6});
			border.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{7, 7, 7, 7});
			border.Margin(winrt::Microsoft::UI::Xaml::Thickness{0, 1, 0, 1});
			border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
				isActive ? winrt::Windows::UI::Color{255, 232, 232, 228}
				         : winrt::Windows::UI::Color{0, 255, 255, 255}));
			border.MinHeight(30);

			winrt::Microsoft::UI::Xaml::Controls::Grid grid;
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition markerCol;
				markerCol.Width(winrt::Microsoft::UI::Xaml::GridLength{14, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
				grid.ColumnDefinitions().Append(markerCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition titleCol;
				titleCol.Width(winrt::Microsoft::UI::Xaml::GridLength{1, winrt::Microsoft::UI::Xaml::GridUnitType::Star});
				grid.ColumnDefinitions().Append(titleCol);
			}
			{
				winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition actionCol;
				actionCol.Width(winrt::Microsoft::UI::Xaml::GridLength{18, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
				grid.ColumnDefinitions().Append(actionCol);
			}
			grid.ColumnSpacing(4);

			winrt::Microsoft::UI::Xaml::Controls::Border marker;
			marker.Width(4);
			marker.Height(4);
			marker.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{2, 2, 2, 2});
			marker.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
				isActive ? winrt::Windows::UI::Color{255, 36, 36, 36}
				         : winrt::Windows::UI::Color{255, 171, 171, 162}));
			marker.Opacity(isActive ? 0.85 : 0.55);
			marker.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			marker.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(marker, 0);
			grid.Children().Append(marker);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock titleBlock;
			titleBlock.Text(winrt::hstring{title.c_str(), static_cast<uint32_t>(title.size())});
			ApplySessionItemTitleStyle(titleBlock);
			titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeight{500});
			titleBlock.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(titleBlock, 1);
			grid.Children().Append(titleBlock);

			winrt::Microsoft::UI::Xaml::Controls::Button closeButton;
			closeButton.Content(winrt::box_value(L"\uE711"));
			closeButton.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
			closeButton.FontSize(9);
			closeButton.Width(18);
			closeButton.Height(18);
			closeButton.Padding(winrt::Microsoft::UI::Xaml::Thickness{0, 0, 0, 0});
			closeButton.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{0, 255, 255, 255}));
			closeButton.BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{0});
			closeButton.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 133, 133, 125}));
			closeButton.Opacity(0);
			closeButton.IsHitTestVisible(false);
			closeButton.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(closeButton, 2);
			closeButton.Click([sessionId, onRemove](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
			{
				if (onRemove)
				{
					onRemove(sessionId);
				}
			});
			closeButton.Tapped([](winrt::Windows::Foundation::IInspectable const&,
			                       winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
			{
				args.Handled(true);
			});
			grid.Children().Append(closeButton);

			border.Child(grid);

			border.PointerEntered([border, closeButton, isActive](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				if (!isActive)
				{
					border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 239, 239, 236}));
				}
				closeButton.Opacity(1);
				closeButton.IsHitTestVisible(true);
			});
			border.PointerExited([border, closeButton, isActive](
				winrt::Windows::Foundation::IInspectable const&,
				winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				border.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
					isActive ? winrt::Windows::UI::Color{255, 232, 232, 228}
					         : winrt::Windows::UI::Color{0, 255, 255, 255}));
				closeButton.Opacity(0);
				closeButton.IsHitTestVisible(false);
			});

			if (onSelect)
			{
				border.Tapped([sessionId, onSelect](
					winrt::Windows::Foundation::IInspectable const&,
					winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
				{
					args.Handled(true);
					onSelect(sessionId);
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
			m_allSessions.push_back(SessionEntry{ {}, std::wstring{title.c_str(), title.size()}, nowMs });
		}
		SyncSplitSessionsWithSessionList();
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
			m_allSessions.push_back(SessionEntry{ {}, std::move(t), ts });
		}
		SyncSplitSessionsWithSessionList();
		RebuildSessionList();
	}

	void Sidebar::SetSessionItems(
		winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> titles,
		winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessionIds,
		winrt::Windows::Foundation::Collections::IVectorView<std::int64_t> createdAtMs)
	{
		m_allSessions.clear();
		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		auto count = titles.Size();
		for (uint32_t i = 0; i < count; ++i)
		{
			std::wstring id;
			if (i < sessionIds.Size())
			{
				auto value = sessionIds.GetAt(i);
				id = std::wstring{value.c_str(), value.size()};
			}

			auto titleValue = titles.GetAt(i);
			std::wstring title{titleValue.c_str(), titleValue.size()};
			if (title.empty())
			{
				title = id;
			}
			std::int64_t ts = (i < createdAtMs.Size()) ? createdAtMs.GetAt(i) : nowMs;
			m_allSessions.push_back(SessionEntry{ std::move(id), std::move(title), ts });
		}
		SyncSplitSessionsWithSessionList();
		RebuildSessionList();
	}

	void Sidebar::SetActiveSession(winrt::hstring title)
	{
		m_activeTitle = std::wstring{title.c_str(), title.size()};
		RebuildSessionList();
	}

	void Sidebar::AddSplitSession(std::wstring sessionId, std::wstring title)
	{
		if (sessionId.empty())
		{
			return;
		}

		if (title.empty())
		{
			title = sessionId;
		}

		for (auto& entry : m_splitSessions)
		{
			if (entry.id == sessionId)
			{
				entry.title = std::move(title);
				RebuildSessionList();
				return;
			}
		}

		m_splitSessions.push_back(SplitEntry{ std::move(sessionId), std::move(title) });
		RebuildSessionList();
	}

	void Sidebar::RebuildSessionList()
	{
		auto todayGroup = this->TodayGroup();
		auto weekGroup = this->WeekGroup();
		auto monthGroup = this->MonthGroup();
		auto earlierGroup = this->EarlierGroup();
		ClearAllGroups(todayGroup, weekGroup, monthGroup, earlierGroup);
		RebuildSplitGroup();

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

			auto active = (!entry.id.empty() && entry.id == m_activeTitle) || entry.title == m_activeTitle;
			auto canAddToSplit = !entry.id.empty() && !active && !IsInSplitGroup(entry.id);
			auto item = MakeSessionItem(
				entry.id,
				entry.title,
				tsStr,
				active,
				canAddToSplit,
				m_onResume,
				m_onAddToSplit,
				m_onFork,
				m_onRename,
				m_onDelete);
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

	void Sidebar::RebuildSplitGroup()
	{
		auto panel = this->SplitGroupPanel();
		auto rows = this->SplitGroupRows();
		rows.Children().Clear();

		auto visible = !m_splitSessions.empty();
		panel.Visibility(visible ? winrt::Microsoft::UI::Xaml::Visibility::Visible
		                         : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
		if (!visible)
		{
			return;
		}

		for (auto const& entry : m_splitSessions)
		{
			auto active = (!entry.id.empty() && entry.id == m_activeTitle) || entry.title == m_activeTitle;
			auto item = MakeSplitItem(
				entry.id,
				entry.title,
				active,
				m_onResume,
				[this](std::wstring sessionId) { RemoveSplitSession(std::move(sessionId)); });
			rows.Children().Append(item);
		}
	}

	void Sidebar::SyncSplitSessionsWithSessionList()
	{
		if (m_splitSessions.empty())
		{
			return;
		}

		auto hadMultiple = m_splitSessions.size() > 1;
		std::vector<SplitEntry> synced;
		synced.reserve(m_splitSessions.size());
		for (auto const& split : m_splitSessions)
		{
			auto found = std::find_if(m_allSessions.begin(), m_allSessions.end(), [&split](SessionEntry const& entry) {
				return !entry.id.empty() && entry.id == split.id;
			});
			if (found == m_allSessions.end())
			{
				continue;
			}

			auto title = found->title.empty() ? found->id : found->title;
			synced.push_back(SplitEntry{ found->id, std::move(title) });
		}

		if (hadMultiple && synced.size() <= 1)
		{
			synced.clear();
		}
		m_splitSessions = std::move(synced);
	}

	void Sidebar::RemoveSplitSession(std::wstring sessionId)
	{
		auto oldSize = m_splitSessions.size();
		m_splitSessions.erase(
			std::remove_if(m_splitSessions.begin(), m_splitSessions.end(), [&sessionId](SplitEntry const& entry) {
				return entry.id == sessionId;
			}),
			m_splitSessions.end());
		if (oldSize != m_splitSessions.size() && m_splitSessions.size() <= 1)
		{
			m_splitSessions.clear();
		}
		RebuildSessionList();
	}

	bool Sidebar::IsInSplitGroup(std::wstring const& sessionId) const
	{
		return std::any_of(m_splitSessions.begin(), m_splitSessions.end(), [&sessionId](SplitEntry const& entry) {
			return entry.id == sessionId;
		});
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

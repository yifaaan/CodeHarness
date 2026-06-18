#include "Controls/Sidebar.xaml.h"

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
	} // namespace

	Sidebar::Sidebar()
	{
		this->InitializeComponent();
	}

	void Sidebar::SetSessions(winrt::Windows::Foundation::Collections::IVectorView<winrt::hstring> sessions)
	{
		auto list = this->SessionsList();
		list.Items().Clear();
		for (auto const& title : sessions)
		{
			winrt::Microsoft::UI::Xaml::Controls::ListViewItem item;
			item.HorizontalContentAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
			item.Padding(winrt::Microsoft::UI::Xaml::Thickness{14, 7, 14, 7});
			item.MinHeight(0);
			item.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{8});
			item.Margin(winrt::Microsoft::UI::Xaml::Thickness{4, 1, 4, 1});

			winrt::Microsoft::UI::Xaml::Controls::TextBlock titleBlock;
			titleBlock.Text(title);
			ApplySessionItemTitleStyle(titleBlock);
			titleBlock.Opacity(0.85);
			item.Content(titleBlock);
			list.Items().Append(item);
		}
	}

	void Sidebar::SetWorkdir(winrt::hstring workdir)
	{
		this->WorkdirText().Text(workdir);
	}

	void Sidebar::Focus()
	{
		this->NewChatButton().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
	}

	void Sidebar::OnNewChatClick(winrt::Windows::Foundation::IInspectable const&,
	                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (m_onNewChat)
		{
			m_onNewChat();
		}
	}

	void Sidebar::OnSessionDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
	                                    winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&)
	{
		auto index = this->SessionsList().SelectedIndex();
		if (index < 0)
		{
			return;
		}
		auto item = this->SessionsList().Items().GetAt(static_cast<uint32_t>(index));
		auto listViewItem = item.try_as<winrt::Microsoft::UI::Xaml::Controls::ListViewItem>();
		if (!listViewItem)
		{
			return;
		}
		auto content = listViewItem.Content();
		auto textBlock = content.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBlock>();
		if (!textBlock)
		{
			return;
		}
		auto text = textBlock.Text();
		std::wstring value{ text.c_str(), text.size() };
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

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

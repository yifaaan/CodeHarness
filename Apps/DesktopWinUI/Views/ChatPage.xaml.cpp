#include "Views/ChatPage.xaml.h"
#include "Controls/ComposerBox.xaml.h"

#include <winrt/base.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	using namespace winrt::Microsoft::UI::Xaml;
	using namespace winrt::Microsoft::UI::Xaml::Controls;

	ChatPage::ChatPage()
	{
		this->InitializeComponent();

		// Forward composer signals to whoever owns the chat (ShellPage).
		auto composerImpl = this->Composer()
		                        .try_as<winrt::CodeHarness::Desktop::Controls::implementation::ComposerBox>();
		if (composerImpl)
		{
			composerImpl->OnSubmit([this](std::wstring text) {
				if (m_onSubmit)
				{
					m_onSubmit(std::move(text));
				}
			});
			composerImpl->OnCancel([this]() {
				if (m_onCancel)
				{
					m_onCancel();
				}
			});
		}
	}

	void ChatPage::SetRunning(bool running)
	{
		m_running = running;
		this->Composer().SetRunning(running);
		ShowStatus(running ? L"Running" : L"Ready");
	}

	void ChatPage::SetEmptyState(bool empty)
	{
		if (auto panel = this->EmptyStatePanel())
		{
			panel.Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
		}
		if (auto scroll = this->MessagesScroll())
		{
			scroll.Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
		}
	}

	void ChatPage::SetStatus(winrt::hstring status)
	{
		ShowStatus(status.empty() ? std::wstring{} : std::wstring{ status.c_str(), status.size() });
	}

	void ChatPage::AppendUserMessage(winrt::hstring text)
	{
		AppendStatusMessage(text); // same render path; only the bubble alignment differs via the subtle flag
	}

	void ChatPage::AppendStatusMessage(winrt::hstring text)
	{
		const bool subtle = false; // user prompt -> right-aligned green bubble
		Border bubble;
		bubble.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{12, 12, 12, 12});
		bubble.Padding(winrt::Microsoft::UI::Xaml::Thickness{14, 10, 14, 10});
		bubble.MaxWidth(760);
		bubble.HorizontalAlignment(subtle ? HorizontalAlignment::Stretch : HorizontalAlignment::Right);
		bubble.Background(Media::SolidColorBrush(Windows::UI::Color{255, 234, 247, 239}));

		TextBlock textBlock;
		textBlock.Text(text);
		textBlock.TextWrapping(TextWrapping::Wrap);
		textBlock.FontSize(14);
		textBlock.IsTextSelectionEnabled(true);
		textBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		bubble.Child(textBlock);
		this->MessagesPanel().Children().Append(bubble);
		ScrollMessagesToBottom();
		m_assistantBubbleOpen = false;
	}

	void ChatPage::AppendAssistantDelta(winrt::hstring delta)
	{
		SetEmptyState(false);
		m_currentAssistantText += std::wstring{ delta.c_str(), delta.size() };
		if (!m_assistantBubbleOpen)
		{
			Border bubble;
			bubble.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{12, 12, 12, 12});
			bubble.Padding(winrt::Microsoft::UI::Xaml::Thickness{14, 10, 14, 10});
			bubble.MaxWidth(760);
			bubble.HorizontalAlignment(HorizontalAlignment::Stretch);
			bubble.Background(Media::SolidColorBrush(Windows::UI::Color{255, 246, 246, 245}));

			TextBlock textBlock;
			textBlock.Text(ToHstring(m_currentAssistantText));
			textBlock.TextWrapping(TextWrapping::Wrap);
			textBlock.FontSize(14);
			textBlock.IsTextSelectionEnabled(true);
			textBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
			bubble.Child(textBlock);
			this->MessagesPanel().Children().Append(bubble);
			m_assistantBubbles.push_back(bubble);
			m_assistantBubbleOpen = true;
		}
		else if (!m_assistantBubbles.empty())
		{
			auto bubble = m_assistantBubbles.back();
			if (auto textBlock = bubble.Child().try_as<TextBlock>())
			{
				textBlock.Text(ToHstring(m_currentAssistantText));
			}
		}
		ScrollMessagesToBottom();
	}

	void ChatPage::AppendToolCard(winrt::hstring name, winrt::hstring detail, bool isError)
	{
		m_assistantBubbleOpen = false;

		Border card;
		auto appResources = winrt::Microsoft::UI::Xaml::Application::Current().Resources();
		auto styleKey = isError ? winrt::box_value(L"ToolCardErrorBorderStyle")
		                        : winrt::box_value(L"ToolCardBorderStyle");
		if (auto lookup = appResources.TryLookup(styleKey))
		{
			if (auto style = lookup.try_as<winrt::Microsoft::UI::Xaml::Style>())
			{
				card.Style(style);
			}
		}
		card.MaxWidth(760);
		card.HorizontalAlignment(HorizontalAlignment::Stretch);

		Grid grid;
		ColumnDefinition iconCol;
		iconCol.Width(GridLength{24, GridUnitType::Pixel});
		ColumnDefinition nameCol;
		nameCol.Width(GridLength{1, GridUnitType::Star});
		ColumnDefinition statusCol;
		statusCol.Width(GridLength{0, GridUnitType::Auto});
		grid.ColumnDefinitions().Append(iconCol);
		grid.ColumnDefinitions().Append(nameCol);
		grid.ColumnDefinitions().Append(statusCol);
		grid.ColumnSpacing(8);
		grid.VerticalAlignment(VerticalAlignment::Center);

		TextBlock icon;
		icon.Text(isError ? L"\uE783" : L"\uE9D9");
		icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
		icon.FontSize(12);
		icon.Foreground(Media::SolidColorBrush(isError ? Windows::UI::Color{255, 191, 22, 22}
		                                              : Windows::UI::Color{255, 22, 163, 74}));
		icon.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(icon, 0);
		grid.Children().Append(icon);

		TextBlock nameBlock;
		nameBlock.Text(name);
		nameBlock.FontSize(13);
		nameBlock.FontWeight(winrt::Windows::UI::Text::FontWeight{600});
		nameBlock.TextTrimming(TextTrimming::CharacterEllipsis);
		nameBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		nameBlock.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(nameBlock, 1);
		grid.Children().Append(nameBlock);

		TextBlock statusBlock;
		statusBlock.Text(detail);
		statusBlock.FontSize(12);
		statusBlock.Foreground(Media::SolidColorBrush(isError ? Windows::UI::Color{255, 191, 22, 22}
		                                                     : Windows::UI::Color{255, 138, 138, 134}));
		statusBlock.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(statusBlock, 2);
		grid.Children().Append(statusBlock);

		card.Child(grid);
		this->MessagesPanel().Children().Append(card);
		ScrollMessagesToBottom();
	}

	void ChatPage::ResetTranscript()
	{
		this->MessagesPanel().Children().Clear();
		m_assistantBubbles.clear();
		m_currentAssistantText.clear();
		m_assistantBubbleOpen = false;
		SetEmptyState(true);
	}

	void ChatPage::FocusComposer()
	{
		this->Composer().FocusPrompt();
	}

	void ChatPage::OnSubmit(std::function<void(std::wstring)> cb)
	{
		m_onSubmit = std::move(cb);
	}

	void ChatPage::OnCancel(std::function<void()> cb)
	{
		m_onCancel = std::move(cb);
	}

	void ChatPage::OnSuggestionClick(winrt::Windows::Foundation::IInspectable const& sender,
	                                 RoutedEventArgs const&)
	{
		if (m_running)
		{
			return;
		}
		auto button = sender.try_as<Button>();
		if (!button)
		{
			return;
		}
		auto tag = button.Tag();
		if (!tag)
		{
			return;
		}
		auto text = winrt::unbox_value_or<winrt::hstring>(tag, L"");
		if (text.empty())
		{
			return;
		}
		if (m_onSubmit)
		{
			m_onSubmit(std::wstring{ text.c_str(), text.size() });
		}
	}

	void ChatPage::ScrollMessagesToBottom()
	{
		if (auto scroll = this->MessagesScroll())
		{
			scroll.ChangeView(nullptr, scroll.ScrollableHeight(), nullptr);
		}
	}

	void ChatPage::ShowStatus(std::wstring const& text)
	{
		this->StatusText().Text(winrt::hstring{ text.c_str(), static_cast<uint32_t>(text.size()) });
	}

	std::wstring ChatPage::ToStd(winrt::hstring const& text) const
	{
		return std::wstring{ text.c_str(), text.size() };
	}

	winrt::hstring ChatPage::ToHstring(std::wstring const& text) const
	{
		return winrt::hstring{ text.c_str(), static_cast<uint32_t>(text.size()) };
	}

} // namespace winrt::CodeHarness::Desktop::Views::implementation

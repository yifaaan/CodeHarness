#include "Views/ChatPage.xaml.h"
#include "Controls/ComposerBox.xaml.h"

#include <winrt/base.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
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

	// ──────────────────────── State helpers ────────────────────────

	void ChatPage::SetRunning(bool running)
	{
		m_running = running;
		this->Composer().SetRunning(running);
		ShowStatus(running ? L"运行中" : L"就绪");
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

	void ChatPage::SetPageTitle(winrt::hstring title)
	{
		this->PageTitleText().Text(title);
	}

	void ChatPage::SetBranchName(winrt::hstring branch)
	{
		this->BranchTabText().Text(branch);
	}

	void ChatPage::SetWorkspaceName(winrt::hstring name)
	{
		this->WorkspaceTabText().Text(name);
	}

	void ChatPage::SetUsage(winrt::hstring text)
	{
		if (auto usage = this->UsageText())
		{
			usage.Text(text);
		}
	}

	// ──────────────────────── User messages ────────────────────────

	void ChatPage::AppendUserMessage(winrt::hstring text)
	{
		AppendStatusMessage(text);
	}

	void ChatPage::AppendStatusMessage(winrt::hstring text)
	{
		Border bubble;
		winrt::Microsoft::UI::Xaml::CornerRadius radius{12, 12, 12, 12};
		bubble.CornerRadius(radius);
		bubble.Padding(Thickness{14, 10, 14, 10});
		bubble.MaxWidth(760);
		bubble.HorizontalAlignment(HorizontalAlignment::Right);
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

	// ──────────────────────── Assistant streaming ────────────────────

	void ChatPage::AppendAssistantDelta(winrt::hstring delta)
	{
		SetEmptyState(false);
		m_currentAssistantText += std::wstring{ delta.c_str(), delta.size() };

		if (!m_assistantBubbleOpen)
		{
			// Close any open thinking/tool state first.
			CloseThinkingBubble();
			CloseToolCard();

			Border bubble;
			winrt::Microsoft::UI::Xaml::CornerRadius radius{12, 12, 12, 12};
			bubble.CornerRadius(radius);
			bubble.Padding(Thickness{14, 10, 14, 10});
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

	void ChatPage::AppendAssistantComplete()
	{
		if (!m_assistantBubbleOpen || m_assistantBubbles.empty())
		{
			return;
		}
		auto bubble = m_assistantBubbles.back();
		auto oldChild = bubble.Child();

		// Replace TextBlock with RichTextBlock for Markdown rendering.
		RichTextBlock rtb;
		rtb.TextWrapping(TextWrapping::Wrap);
		rtb.FontSize(14);
		rtb.IsTextSelectionEnabled(true);
		rtb.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		RenderMarkdownToRichText(rtb, m_currentAssistantText);
		bubble.Child(rtb);
		m_assistantBubbleOpen = false;
	}

	// ──────────────────────── Thinking blocks ────────────────────────

	void ChatPage::CloseThinkingBubble()
	{
		if (!m_thinkingBubbleOpen)
		{
			return;
		}
		m_thinkingBubbleOpen = false;
		m_currentThinkingExpander = nullptr;
	}

	void ChatPage::AppendThinkingBlock(winrt::hstring content, std::int64_t durationMs)
	{
		SetEmptyState(false);
		m_currentAssistantText.clear();

		// Format duration display.
		std::wstring durationStr;
		if (durationMs > 0)
		{
			double secs = static_cast<double>(durationMs) / 1000.0;
			wchar_t buf[32];
			if (secs < 10.0)
				swprintf_s(buf, L"%.1fs", secs);
			else
				swprintf_s(buf, L"%ds", static_cast<int>(secs + 0.5));
			durationStr = buf;
		}
		else
		{
			durationStr = L"思考中...";
		}

		// Build the collapsed header: triangle icon + "思考过程" + duration.
		StackPanel headerStack;
		headerStack.Orientation(Orientation::Horizontal);
		headerStack.Spacing(8);

		TextBlock chevron;
		chevron.Text(L"\uE70D");
		chevron.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
		chevron.FontSize(10);
		chevron.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		chevron.VerticalAlignment(VerticalAlignment::Center);
		headerStack.Children().Append(chevron);

		TextBlock label;
		label.Text(L"思考过程");
		label.FontSize(13);
		label.FontWeight(Windows::UI::Text::FontWeight{500});
		label.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		headerStack.Children().Append(label);

		TextBlock duration;
		duration.Text(ToHstring(durationStr));
		duration.FontSize(12);
		duration.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		headerStack.Children().Append(duration);

		// Content: thinking text (only visible when expanded).
		TextBlock contentBlock;
		contentBlock.Text(content);
		contentBlock.TextWrapping(TextWrapping::Wrap);
		contentBlock.FontSize(13);
		contentBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 100, 100, 100}));
		contentBlock.Margin(Thickness{24, 4, 4, 4});

		// Assemble Expander.
		Expander expander;
		expander.Header(headerStack);
		expander.Content(contentBlock);
		expander.HorizontalAlignment(HorizontalAlignment::Stretch);
		expander.MaxWidth(760);
		expander.Margin(Thickness{0, 4, 0, 4});

		this->MessagesPanel().Children().Append(expander);
		m_currentThinkingExpander = expander;
		m_thinkingBubbleOpen = true;
		ScrollMessagesToBottom();
	}

	// ──────────────────────── Tool cards ────────────────────────

	void ChatPage::CloseToolCard()
	{
		m_toolCardExpanded = false;
		m_currentToolCard = nullptr;
	}

	void ChatPage::AppendToolCard(winrt::hstring name, winrt::hstring detail, bool isError,
	                               winrt::hstring fullOutput)
	{
		SetEmptyState(false);
		m_assistantBubbleOpen = false;
		m_currentAssistantText.clear();
		CloseToolCard();

		// Outer card border.
		Border card;
		auto appResources = Application::Current().Resources();
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

		// Root Grid: Row 0 = header, Row 1 = expandable output.
		Grid grid;
		{
			RowDefinition headerRow;
			headerRow.Height(GridLength{0, GridUnitType::Auto});
			grid.RowDefinitions().Append(headerRow);
		}
		{
			RowDefinition outputRow;
			outputRow.Height(GridLength{0, GridUnitType::Auto});
			grid.RowDefinitions().Append(outputRow);
		}

		// ── Header row (clickable): chevron + icon + name + status dot. ──
		Grid headerGrid;
		{
			ColumnDefinition chevronCol;
			chevronCol.Width(GridLength{20, GridUnitType::Pixel});
			headerGrid.ColumnDefinitions().Append(chevronCol);
		}
		{
			ColumnDefinition iconCol;
			iconCol.Width(GridLength{22, GridUnitType::Pixel});
			headerGrid.ColumnDefinitions().Append(iconCol);
		}
		{
			ColumnDefinition nameCol;
			nameCol.Width(GridLength{1, GridUnitType::Star});
			headerGrid.ColumnDefinitions().Append(nameCol);
		}
		{
			ColumnDefinition statusCol;
			statusCol.Width(GridLength{0, GridUnitType::Auto});
			headerGrid.ColumnDefinitions().Append(statusCol);
		}
		headerGrid.ColumnSpacing(6);
		headerGrid.VerticalAlignment(VerticalAlignment::Center);
		headerGrid.Margin(Thickness{2, 4, 2, 4});
		Grid::SetRow(headerGrid, 0);

		// Expand/collapse chevron.
		TextBlock chevron;
		chevron.Text(L"\uE70D");
		chevron.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
		chevron.FontSize(9);
		chevron.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		chevron.VerticalAlignment(VerticalAlignment::Center);
		chevron.HorizontalAlignment(HorizontalAlignment::Center);
		Grid::SetColumn(chevron, 0);
		headerGrid.Children().Append(chevron);

		// Status icon.
		TextBlock icon;
		icon.Text(isError ? L"\uE783" : L"\uE9D9");
		icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
		icon.FontSize(12);
		icon.Foreground(Media::SolidColorBrush(isError ? Windows::UI::Color{255, 191, 22, 22}
		                                               : Windows::UI::Color{255, 22, 163, 74}));
		icon.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(icon, 1);
		headerGrid.Children().Append(icon);

		// Tool name.
		TextBlock nameBlock;
		nameBlock.Text(name);
		nameBlock.FontSize(13);
		nameBlock.FontWeight(Windows::UI::Text::FontWeight{600});
		nameBlock.TextTrimming(TextTrimming::CharacterEllipsis);
		nameBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		nameBlock.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(nameBlock, 2);
		headerGrid.Children().Append(nameBlock);

		// Status indicator.
		TextBlock statusBlock;
		statusBlock.Text(detail);
		statusBlock.FontSize(12);
		statusBlock.Foreground(Media::SolidColorBrush(isError ? Windows::UI::Color{255, 191, 22, 22}
		                                                      : Windows::UI::Color{255, 138, 138, 134}));
		statusBlock.VerticalAlignment(VerticalAlignment::Center);
		Grid::SetColumn(statusBlock, 3);
		headerGrid.Children().Append(statusBlock);

		grid.Children().Append(headerGrid);

		// ── Expandable output row (hidden by default). ──
		Border outputBorder;
		outputBorder.Visibility(Visibility::Collapsed);
		outputBorder.Margin(Thickness{24, 4, 4, 4});
		outputBorder.MaxWidth(700);

		TextBlock outputText;
		outputText.Text(fullOutput);
		outputText.TextWrapping(TextWrapping::Wrap);
		outputText.FontSize(12);
		outputText.FontFamily(Media::FontFamily(L"Consolas"));
		outputText.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 60, 60, 56}));
		outputText.IsTextSelectionEnabled(true);
		outputBorder.Child(outputText);
		Grid::SetRow(outputBorder, 1);
		grid.Children().Append(outputBorder);

		// Bind Tapped on the header to toggle expand/collapse.
		m_currentToolCard = grid;
		headerGrid.Tapped([this, outputBorder, chevron](Windows::Foundation::IInspectable const&,
		                                                 Input::TappedRoutedEventArgs const&)
		{
			m_toolCardExpanded = !m_toolCardExpanded;
			outputBorder.Visibility(m_toolCardExpanded ? Visibility::Visible : Visibility::Collapsed);
			chevron.Text(m_toolCardExpanded ? L"\uE70E" : L"\uE70D");
		});

		card.Child(grid);
		this->MessagesPanel().Children().Append(card);
		ScrollMessagesToBottom();
	}

	// ──────────────────────── Timestamp ────────────────────────

	void ChatPage::AppendTimestamp(winrt::hstring text)
	{
		m_assistantBubbleOpen = false;
		CloseToolCard();
		m_currentAssistantText.clear();

		TextBlock ts;
		ts.Text(text);
		ts.FontSize(11);
		ts.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		ts.HorizontalAlignment(HorizontalAlignment::Center);
		ts.Margin(Thickness{0, 8, 0, 8});
		this->MessagesPanel().Children().Append(ts);
		ScrollMessagesToBottom();
	}

	// ──────────────────────── Reset ────────────────────────

	void ChatPage::ResetTranscript()
	{
		this->MessagesPanel().Children().Clear();
		m_assistantBubbles.clear();
		m_currentAssistantText.clear();
		m_assistantBubbleOpen = false;
		m_thinkingBubbleOpen = false;
		m_currentThinkingExpander = nullptr;
		m_toolCardExpanded = false;
		m_currentToolCard = nullptr;
		SetEmptyState(true);
	}

	// ──────────────────────── Composer forwarding ────────────────────

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

	// ──────────────────────── Markdown rendering ────────────────────

	void ChatPage::ParseMarkdownRuns(std::wstring const& text,
	                                 std::vector<winrt::Microsoft::UI::Xaml::Documents::Run>& runs)
	{
		size_t pos = 0;
		size_t len = text.size();

		while (pos < len)
		{
			// ── Fenced code block: ``` ... ``` ──
			if (pos + 2 < len && text[pos] == L'`' && text[pos + 1] == L'`' && text[pos + 2] == L'`')
			{
				pos += 3;
				// Skip optional language tag on the same line.
				auto lineEnd = text.find(L'\n', pos);
				if (lineEnd != std::wstring::npos)
					pos = lineEnd + 1;
				auto closeFence = text.find(L"```", pos);
				std::wstring codeContent;
				if (closeFence != std::wstring::npos)
				{
					codeContent = text.substr(pos, closeFence - pos);
					pos = closeFence + 3;
					// Skip trailing newline.
					if (pos < len && text[pos] == L'\n') pos++;
				}
				else
				{
					codeContent = text.substr(pos);
					pos = len;
				}
				// Trim trailing newlines.
				while (!codeContent.empty() && codeContent.back() == L'\n')
					codeContent.pop_back();

				runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
				runs.back().Text(L"\n");
				runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
				runs.back().Text(ToHstring(codeContent));
				runs.back().FontFamily(Media::FontFamily(L"Consolas"));
				runs.back().FontSize(13);
				runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
				runs.back().Text(L"\n");
				continue;
			}

			// ── Inline code: `...` ──
			if (text[pos] == L'`')
			{
				auto end = text.find(L'`', pos + 1);
				if (end != std::wstring::npos)
				{
					auto codeText = text.substr(pos + 1, end - pos - 1);
					runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
					runs.back().Text(ToHstring(codeText));
					runs.back().FontFamily(Media::FontFamily(L"Consolas"));
					runs.back().FontSize(13);
					// Run has no Background property in WinUI; use a tinted foreground
					// to visually distinguish inline code.
					runs.back().Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 168, 75, 50}));
					pos = end + 1;
					continue;
				}
			}

			// ── Bold: **...** ──
			if (pos + 1 < len && text[pos] == L'*' && text[pos + 1] == L'*')
			{
				auto end = text.find(L"**", pos + 2);
				if (end != std::wstring::npos)
				{
					auto boldText = text.substr(pos + 2, end - pos - 2);
					runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
					runs.back().Text(ToHstring(boldText));
					runs.back().FontWeight(Windows::UI::Text::FontWeights::Bold());
					pos = end + 2;
					continue;
				}
			}

			// ── List item: - or * at start of line ──
			if ((text[pos] == L'-' || text[pos] == L'*') &&
			    (pos == 0 || text[pos - 1] == L'\n'))
			{
				// Find end of this list item line.
				auto lineEnd = text.find(L'\n', pos);
				auto itemText = (lineEnd != std::wstring::npos)
					? text.substr(pos, lineEnd - pos)
					: text.substr(pos);
				runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
				runs.back().Text(ToHstring(itemText));
				pos = (lineEnd != std::wstring::npos) ? lineEnd + 1 : len;
				continue;
			}

			// ── Plain text: consume until next special char. ──
			auto nextSpecial = len;
			for (auto i = pos; i < len; ++i)
			{
				if (text[i] == L'`' || text[i] == L'*')
				{
					nextSpecial = i;
					break;
				}
			}
			auto plain = text.substr(pos, nextSpecial - pos);
			runs.push_back(winrt::Microsoft::UI::Xaml::Documents::Run{});
			runs.back().Text(ToHstring(plain));
			pos = nextSpecial;
		}
	}

	void ChatPage::RenderMarkdownToRichText(RichTextBlock const& rtb, std::wstring const& text)
	{
		std::vector<winrt::Microsoft::UI::Xaml::Documents::Run> runs;
		ParseMarkdownRuns(text, runs);

		auto paragraph = winrt::Microsoft::UI::Xaml::Documents::Paragraph{};
		paragraph.LineHeight(22);
		for (auto& run : runs)
		{
			paragraph.Inlines().Append(run);
		}
		rtb.Blocks().Clear();
		rtb.Blocks().Append(paragraph);
	}

	// ──────────────────────── Utilities ────────────────────────

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
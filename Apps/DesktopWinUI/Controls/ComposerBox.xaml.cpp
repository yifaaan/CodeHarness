#include "Controls/ComposerBox.xaml.h"

#include <algorithm>

#include <winrt/base.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.h>

namespace winrt::CodeHarness::Desktop::Controls::implementation
{

	ComposerBox::ComposerBox()
	{
		this->InitializeComponent();
	}

	void ComposerBox::SetRunning(bool running)
	{
		m_running = running;
		this->SendButton().IsEnabled(!running);
		this->CancelButton().IsEnabled(running);
	}

	void ComposerBox::FocusPrompt()
	{
		this->PromptBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
	}

	void ComposerBox::InsertPromptText(winrt::hstring text)
	{
		InsertPromptToken(std::wstring_view{ text.c_str(), text.size() });
	}

	void ComposerBox::AddContextAttachment(winrt::hstring path, bool directory)
	{
		auto value = std::wstring{ path.c_str(), path.size() };
		while (!value.empty() && (value.back() == L'/' || value.back() == L'\\'))
		{
			value.pop_back();
		}
		if (value.empty())
		{
			return;
		}

		auto exists = std::any_of(m_contextAttachments.begin(), m_contextAttachments.end(),
			[&](ContextAttachment const& attachment) {
				return attachment.path == value && attachment.directory == directory;
			});
		if (!exists)
		{
			m_contextAttachments.push_back(ContextAttachment{ std::move(value), directory });
			RebuildAttachmentChips();
		}
		FocusPrompt();
	}

	winrt::hstring ComposerBox::SelectedModel()
	{
		return winrt::hstring{ m_selectedModel.c_str(), static_cast<uint32_t>(m_selectedModel.size()) };
	}

	void ComposerBox::SetSelectedModel(winrt::hstring model)
	{
		m_selectedModel = std::wstring{ model.c_str(), model.size() };
		this->ModelNameText().Text(model);
		if (m_onModelSelected)
		{
			m_onModelSelected(m_selectedModel);
		}
	}

	bool ComposerBox::IsThinkingEnabled()
	{
		return m_thinkingEnabled;
	}

	void ComposerBox::OnPromptKeyDown(winrt::Windows::Foundation::IInspectable const&,
	                                  winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
	{
		if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
		{
			return;
		}
		const auto shift = (winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(
		                        winrt::Windows::System::VirtualKey::Shift) &
		                    winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) ==
		                   winrt::Windows::UI::Core::CoreVirtualKeyStates::Down;
		const auto ctrl = (winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(
		                           winrt::Windows::System::VirtualKey::Control) &
		                       winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) ==
		                  winrt::Windows::UI::Core::CoreVirtualKeyStates::Down;
		if (shift || ctrl)
		{
			return;
		}
		args.Handled(true);
		SubmitCurrent();
	}

	void ComposerBox::OnSendClick(winrt::Windows::Foundation::IInspectable const&,
	                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SubmitCurrent();
	}

	void ComposerBox::OnCancelClick(winrt::Windows::Foundation::IInspectable const&,
	                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		if (m_onCancel)
		{
			m_onCancel();
		}
	}

	void ComposerBox::OnActionMenuClick(winrt::Windows::Foundation::IInspectable const&,
	                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};

		auto appendItem = [&](std::wstring_view label, std::wstring_view glyph, std::wstring_view token) {
			auto item = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
			item.Text(winrt::hstring{ label });
			winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
			icon.Glyph(winrt::hstring{ glyph });
			item.Icon(icon);
			auto tokenCopy = std::wstring{ token };
			item.Click([this, tokenCopy](auto&&, auto&&) {
				InsertPromptToken(tokenCopy);
			});
			flyout.Items().Append(item);
		};

		appendItem(L"添加上下文", L"\uE8B7", L"@");
		appendItem(L"插入命令", L"\uE943", L"/");
		appendItem(L"调用工具", L"\uE756", L"!");
		flyout.ShowAt(this->ActionMenuButton());
	}

	void ComposerBox::OnModeSelectorClick(winrt::Windows::Foundation::IInspectable const&,
	                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};

		auto codeItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		codeItem.Text(L"Code 模式");
		winrt::Microsoft::UI::Xaml::Controls::FontIcon check;
		check.Glyph(L"\uE73E");
		codeItem.Icon(check);
		flyout.Items().Append(codeItem);

		auto planItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		planItem.Text(L"Plan 模式（暂未接入）");
		planItem.IsEnabled(false);
		winrt::Microsoft::UI::Xaml::Controls::FontIcon planIcon;
		planIcon.Glyph(L"\uE9D2");
		planItem.Icon(planIcon);
		flyout.Items().Append(planItem);

		flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});

		auto hint = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		hint.Text(L"当前会话允许读取、编辑文件并运行工具");
		hint.IsEnabled(false);
		flyout.Items().Append(hint);

		flyout.ShowAt(this->ModeSelectorButton());
	}

	void ComposerBox::SetAvailableModels(std::vector<std::wstring> models)
	{
		m_models = std::move(models);
		if (m_models.empty())
		{
			return;
		}
		// If the current selection isn't in the new list, snap to the first entry.
		bool found = false;
		for (auto const& m : m_models)
		{
			if (m == m_selectedModel) { found = true; break; }
		}
		if (!found)
		{
			SetSelectedModel(winrt::hstring{ m_models.front().c_str(),
			                                 static_cast<uint32_t>(m_models.front().size()) });
		}
	}

	void ComposerBox::OnModelSelectorClick(winrt::Windows::Foundation::IInspectable const&,
	                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};

		for (auto const& m : m_models)
		{
			auto item = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
			item.Text(winrt::hstring{ m.c_str(), static_cast<uint32_t>(m.size()) });
			if (m == m_selectedModel)
			{
				winrt::Microsoft::UI::Xaml::Controls::FontIcon check;
				check.Glyph(L"\uE73E");
				item.Icon(check);
			}
			auto modelCopy = m;
			item.Click([this, modelCopy](auto&&, auto&&) {
				SetSelectedModel(winrt::hstring{ modelCopy.c_str(), static_cast<uint32_t>(modelCopy.size()) });
			});
			flyout.Items().Append(item);
		}
		flyout.ShowAt(this->ModelSelectorButton());
	}

	void ComposerBox::OnPermissionSelectorClick(winrt::Windows::Foundation::IInspectable const&,
	                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		auto flyout = winrt::Microsoft::UI::Xaml::Controls::MenuFlyout{};

		auto manualItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		manualItem.Text(L"Manual");
		winrt::Microsoft::UI::Xaml::Controls::FontIcon manualIcon;
		manualIcon.Glyph(L"\uE73E");
		manualItem.Icon(manualIcon);
		flyout.Items().Append(manualItem);

		auto fullAccessItem = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		fullAccessItem.Text(L"Full Access（暂未接入）");
		fullAccessItem.IsEnabled(false);
		winrt::Microsoft::UI::Xaml::Controls::FontIcon warningIcon;
		warningIcon.Glyph(L"\uE7BA");
		fullAccessItem.Icon(warningIcon);
		flyout.Items().Append(fullAccessItem);

		flyout.Items().Append(winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSeparator{});

		auto hint = winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem{};
		hint.Text(L"写入文件、执行命令等操作会先请求确认");
		hint.IsEnabled(false);
		flyout.Items().Append(hint);

		flyout.ShowAt(this->PermissionSelectorButton());
	}

	void ComposerBox::OnThinkingClick(winrt::Windows::Foundation::IInspectable const&,
	                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		m_thinkingEnabled = !m_thinkingEnabled;
		auto accent = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
			Windows::UI::Color{255, 37, 37, 37} }; // CodePilot-style neutral primary
		auto muted = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
			Windows::UI::Color{255, 138, 138, 134} }; // muted grey
		this->ThinkingDot().Fill(m_thinkingEnabled ? accent : muted);
		if (m_onThinkingToggled)
		{
			m_onThinkingToggled(m_thinkingEnabled);
		}
	}

	void ComposerBox::SubmitCurrent()
	{
		if (m_running)
		{
			return;
		}
		auto text = this->PromptBox().Text();
		auto value = std::wstring{ text.c_str(), text.size() };
		auto attachmentPrefix = BuildAttachmentPrefix();
		if (value.empty() && attachmentPrefix.empty())
		{
			return;
		}
		this->PromptBox().Text(L"");
		if (!attachmentPrefix.empty())
		{
			value = value.empty() ? std::move(attachmentPrefix) : attachmentPrefix + L"\n" + value;
			m_contextAttachments.clear();
			RebuildAttachmentChips();
		}
		if (m_onSubmit)
		{
			m_onSubmit(std::move(value));
		}
	}

	void ComposerBox::InsertPromptToken(std::wstring_view token)
	{
		auto current = this->PromptBox().Text();
		auto text = std::wstring{ current.c_str(), current.size() };
		if (!text.empty() && text.back() != L' ' && text.back() != L'\n')
		{
			text += L" ";
		}
		text += token;
		if (token != std::wstring_view{L"/"} && token != std::wstring_view{L"!"})
		{
			text += L" ";
		}
		this->PromptBox().Text(winrt::hstring{ text });
		this->PromptBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
		this->PromptBox().SelectionStart(static_cast<int32_t>(text.size()));
	}

	void ComposerBox::RemoveContextAttachment(std::wstring const& path, bool directory)
	{
		auto nextEnd = std::remove_if(m_contextAttachments.begin(), m_contextAttachments.end(),
			[&](ContextAttachment const& attachment) {
				return attachment.path == path && attachment.directory == directory;
			});
		if (nextEnd == m_contextAttachments.end())
		{
			return;
		}
		m_contextAttachments.erase(nextEnd, m_contextAttachments.end());
		RebuildAttachmentChips();
	}

	void ComposerBox::RebuildAttachmentChips()
	{
		auto panel = this->AttachmentPanel();
		panel.Children().Clear();
		this->AttachmentScroll().Visibility(m_contextAttachments.empty()
			? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
			: winrt::Microsoft::UI::Xaml::Visibility::Visible);

		for (auto const& attachment : m_contextAttachments)
		{
			winrt::Microsoft::UI::Xaml::Controls::Border pill;
			pill.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{12, 12, 12, 12});
			pill.Padding(winrt::Microsoft::UI::Xaml::Thickness{8, 3, 4, 3});
			pill.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{255, 241, 241, 238}));
			pill.BorderBrush(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{255, 224, 224, 219}));
			pill.BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{1});

			winrt::Microsoft::UI::Xaml::Controls::Grid grid;
			winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition iconCol;
			iconCol.Width(winrt::Microsoft::UI::Xaml::GridLength{0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto});
			grid.ColumnDefinitions().Append(iconCol);
			winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition textCol;
			textCol.Width(winrt::Microsoft::UI::Xaml::GridLength{0, winrt::Microsoft::UI::Xaml::GridUnitType::Auto});
			grid.ColumnDefinitions().Append(textCol);
			winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition closeCol;
			closeCol.Width(winrt::Microsoft::UI::Xaml::GridLength{20, winrt::Microsoft::UI::Xaml::GridUnitType::Pixel});
			grid.ColumnDefinitions().Append(closeCol);
			grid.ColumnSpacing(6);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock icon;
			icon.Text(attachment.directory ? L"\uE8B7" : L"\uE8A5");
			icon.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
			icon.FontSize(12);
			icon.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{255, 112, 112, 105}));
			icon.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(icon, 0);
			grid.Children().Append(icon);

			winrt::Microsoft::UI::Xaml::Controls::TextBlock label;
			label.Text(winrt::hstring{ attachment.path });
			label.FontSize(12);
			label.MaxWidth(220);
			label.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
			label.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{255, 50, 50, 47}));
			label.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(label, 1);
			grid.Children().Append(label);

			winrt::Microsoft::UI::Xaml::Controls::Button closeButton;
			closeButton.Content(winrt::box_value(L"\uE711"));
			closeButton.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets"));
			closeButton.FontSize(9);
			closeButton.Width(20);
			closeButton.Height(20);
			closeButton.Padding(winrt::Microsoft::UI::Xaml::Thickness{0, 0, 0, 0});
			closeButton.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{0, 255, 255, 255}));
			closeButton.BorderThickness(winrt::Microsoft::UI::Xaml::Thickness{0});
			closeButton.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(Windows::UI::Color{255, 112, 112, 105}));
			closeButton.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
			closeButton.Click([this, path = attachment.path, directory = attachment.directory](auto&&, auto&&) {
				RemoveContextAttachment(path, directory);
			});
			winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(closeButton, 2);
			grid.Children().Append(closeButton);

			pill.Child(grid);
			panel.Children().Append(pill);
		}
	}

	std::wstring ComposerBox::BuildAttachmentPrefix() const
	{
		std::wstring prefix;
		for (auto const& attachment : m_contextAttachments)
		{
			if (!prefix.empty())
			{
				prefix += L" ";
			}
			prefix += L"@";
			prefix += attachment.path;
			if (attachment.directory)
			{
				prefix += L"/";
			}
		}
		return prefix;
	}

} // namespace winrt::CodeHarness::Desktop::Controls::implementation

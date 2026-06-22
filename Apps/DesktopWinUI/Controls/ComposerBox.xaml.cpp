#include "Controls/ComposerBox.xaml.h"

#include <winrt/base.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
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

	void ComposerBox::OnThinkingClick(winrt::Windows::Foundation::IInspectable const&,
	                                  winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		m_thinkingEnabled = !m_thinkingEnabled;
		auto accent = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
			Windows::UI::Color{255, 37, 99, 235} }; // CodeHarness blue
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
		if (text.empty())
		{
			return;
		}
		this->PromptBox().Text(L"");
		std::wstring value{ text.c_str(), text.size() };
		if (m_onSubmit)
		{
			m_onSubmit(std::move(value));
		}
	}

} // namespace winrt::CodeHarness::Desktop::Controls::implementation
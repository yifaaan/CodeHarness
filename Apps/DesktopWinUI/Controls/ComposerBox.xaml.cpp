#include "Controls/ComposerBox.xaml.h"

#include <winrt/base.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
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

	void ComposerBox::OnPromptKeyDown(winrt::Windows::Foundation::IInspectable const&,
	                                  winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
	{
		// Enter sends the prompt; Shift+Enter (or Ctrl+Enter) inserts a newline,
		// matching the ChatGLM desktop composer behavior.
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
			return; // let the TextBox insert its newline
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

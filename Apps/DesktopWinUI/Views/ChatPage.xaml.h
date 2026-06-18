#pragma once

#include <unknwn.h>

// The generated ChatPage.g.h references winrt::CodeHarness::Desktop::Controls::ComposerBox,
// so its projection must be visible BEFORE ChatPage.g.h is included.
#include <winrt/CodeHarness.Desktop.Controls.h>

#include "Views.ChatPage.g.h"

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	struct ChatPage : ChatPageT<ChatPage>
	{
		ChatPage();

		// IDL-projected surface (called by ShellPage / MainWindow).
		void SetRunning(bool running);
		void SetEmptyState(bool empty);
		void SetStatus(winrt::hstring status);

		void AppendUserMessage(winrt::hstring text);
		void AppendStatusMessage(winrt::hstring text);
		void AppendAssistantDelta(winrt::hstring text);
		void AppendToolCard(winrt::hstring name, winrt::hstring detail, bool isError);
		void ResetTranscript();
		void FocusComposer();

		// C++-only callbacks for composer + suggestion interactions.
		void OnSubmit(std::function<void(std::wstring)> cb);
		void OnCancel(std::function<void()> cb);

		// XAML-wired handler.
		void OnSuggestionClick(winrt::Windows::Foundation::IInspectable const& sender,
		                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

	private:
		void ScrollMessagesToBottom();
		void ShowStatus(std::wstring const& text);
		std::wstring ToStd(winrt::hstring const& text) const;
		winrt::hstring ToHstring(std::wstring const& text) const;

		bool m_running = false;
		bool m_assistantBubbleOpen = false;
		std::wstring m_currentAssistantText;
		std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> m_assistantBubbles;
		std::function<void(std::wstring)> m_onSubmit;
		std::function<void()> m_onCancel;
	};

} // namespace winrt::CodeHarness::Desktop::Views::implementation

namespace winrt::CodeHarness::Desktop::Views::factory_implementation
{

	struct ChatPage : ChatPageT<ChatPage, implementation::ChatPage>
	{
	};

} // namespace winrt::CodeHarness::Desktop::Views::factory_implementation

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
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>

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
		void AppendAssistantComplete();
		void AppendThinkingBlock(winrt::hstring content, std::int64_t durationMs);
		void AppendToolCard(winrt::hstring name, winrt::hstring detail, bool isError, winrt::hstring fullOutput);
		void AppendTimestamp(winrt::hstring text);
		void ResetTranscript();
		void FocusComposer();
		void InsertComposerText(winrt::hstring text);
		void AddComposerAttachment(winrt::hstring path, bool directory);
		void SetPageTitle(winrt::hstring title);
		void SetBranchName(winrt::hstring branch);
		void SetWorkspaceName(winrt::hstring name);
		void SetUsage(winrt::hstring text);

		// C++-only callbacks for composer + suggestion interactions.
		void OnSubmit(std::function<void(std::wstring)> cb);
		void OnCancel(std::function<void()> cb);

		// XAML-wired handler.
		void OnSuggestionClick(winrt::Windows::Foundation::IInspectable const& sender,
		                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

	private:
		void ScrollMessagesToBottom();
		void ShowStatus(std::wstring const& text);
		void UpdateWelcomeText();
		std::wstring ToStd(winrt::hstring const& text) const;
		winrt::hstring ToHstring(std::wstring const& text) const;
		void RenderMarkdownToRichText(winrt::Microsoft::UI::Xaml::Controls::RichTextBlock const& rtb,
		                              std::wstring const& text);
		void ParseMarkdownRuns(std::wstring const& text,
		                       std::vector<winrt::Microsoft::UI::Xaml::Documents::Run>& runs);
		void CloseThinkingBubble();
		void CloseToolCard();

		bool m_running = false;
		bool m_assistantBubbleOpen = false;
		bool m_thinkingBubbleOpen = false;
		std::wstring m_workspaceName = L"CodeHarness";
		std::wstring m_currentAssistantText;
		std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> m_assistantBubbles;
		winrt::Microsoft::UI::Xaml::Controls::Expander m_currentThinkingExpander{ nullptr };
		winrt::Microsoft::UI::Xaml::Controls::Grid m_currentToolCard{ nullptr };
		bool m_toolCardExpanded = false;
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

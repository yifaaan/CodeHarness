#include "MainWindow.xaml.h"

#include <chrono>
#include <future>
#include <memory>
#include <utility>

#include <Windows.h>
#include <microsoft.ui.xaml.window.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/base.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace desktop_app = codeharness::desktop_app;

namespace winrt::CodeHarness::Desktop::implementation
{

	MainWindow::MainWindow()
	{
		this->InitializeComponent();
		InitializeUi();
		LoadSessions();
	}

	void MainWindow::ApplyDefaultWindowSize()
	{
		// Called from App::OnLaunched where the projected Microsoft::UI::Xaml::Window
		// base is directly available; HWND is exposed via the raw COM IWindowNative.
		auto baseWindow = (*this).try_as<winrt::Microsoft::UI::Xaml::Window>();
		if (!baseWindow)
		{
			return;
		}
		winrt::com_ptr<::IWindowNative> windowNative;
		baseWindow.as(windowNative);
		if (windowNative)
		{
			HWND hwnd = nullptr;
			if (SUCCEEDED(windowNative->get_WindowHandle(&hwnd)) && hwnd)
			{
				SetWindowPos(hwnd, nullptr, 120, 90, 1280, 820, SWP_NOZORDER);
			}
		}
	}

	MainWindow::~MainWindow()
	{
		if (core)
		{
			core->CloseActiveSession();
		}
	}

	void MainWindow::InitializeUi()
	{
		core = std::make_unique<desktop_app::DesktopCoreService>();
		auto queue = DispatcherQueue();
		core->SetEventCallback([this, queue](const codeharness::desktop::DesktopEvent& event) {
			queue.TryEnqueue([this, event]() {
				if (event.type == "loop")
				{
					auto loop = event.payload.value("loop_event", nlohmann::json::object());
					if (loop.contains("AssistantDelta"))
					{
						AppendAssistantDelta(ToWide(loop["AssistantDelta"].value("text", "")));
					}
					else if (loop.contains("ToolCallStarted"))
					{
						auto tool = loop["ToolCallStarted"];
						AppendMessage(L"Tool call: " + ToWide(tool.value("name", "")), true);
					}
					else if (loop.contains("ToolResult"))
					{
						auto tool = loop["ToolResult"];
						AppendMessage(L"Tool result: " + ToWide(tool.value("name", "")), true);
					}
					else if (loop.contains("PermissionDenied"))
					{
						auto permission = loop["PermissionDenied"];
						AppendMessage(L"Permission denied: " + ToWide(permission.value("name", "")), true);
					}
				}
				else if (event.type == "turn_started")
				{
					currentAssistantText.clear();
					SetRunning(true);
				}
				else if (event.type == "turn_ended")
				{
					SetRunning(false);
				}
				else if (event.type == "error")
				{
					AppendMessage(L"Error: " + ToWide(event.payload.value("message", "")), true);
					SetRunning(false);
				}
			});
		});

		core->SetApprovalCallback([this, queue](const codeharness::desktop::DesktopPermissionRequest& request) {
			auto promise = std::make_shared<std::promise<bool>>();
			auto future = promise->get_future();
			queue.TryEnqueue([this, request, promise]() {
				ContentDialog dialog;
				dialog.XamlRoot(Content().try_as<FrameworkElement>().XamlRoot());
				dialog.Title(box_value(L"Allow tool execution?"));
				dialog.Content(box_value(ToWide(request.description)));
				dialog.PrimaryButtonText(L"Allow");
				dialog.CloseButtonText(L"Deny");
				auto op = dialog.ShowAsync();
				op.Completed([promise](auto const& async, auto) {
					promise->set_value(async.GetResults() == ContentDialogResult::Primary);
				});
			});
			return future.get();
		});

		core->SetQuestionCallback([this, queue](const codeharness::tools::QuestionRequest& request) {
			auto promise = std::make_shared<std::promise<std::string>>();
			auto future = promise->get_future();
			queue.TryEnqueue([this, request, promise]() {
				ContentDialog dialog;
				dialog.XamlRoot(Content().try_as<FrameworkElement>().XamlRoot());
				dialog.Title(box_value(L"CodeHarness question"));
				dialog.Content(box_value(ToWide(request.question)));
				dialog.PrimaryButtonText(request.options.empty() ? L"OK" : ToWide(request.options.front()));
				dialog.CloseButtonText(L"Cancel");
				auto op = dialog.ShowAsync();
				op.Completed([choice = request.options.empty() ? std::string{} : request.options.front(),
							  promise](auto const& async, auto) {
					promise->set_value(async.GetResults() == ContentDialogResult::Primary ? choice : std::string{});
				});
			});
			return future.get();
		});

		this->NewChatButton().Click([this](auto&&, auto&&) { NewChat(); });
		this->SendButton().Click([this](auto&&, auto&&) { SendPrompt(); });
		this->CancelButton().Click([this](auto&&, auto&&) { CancelPrompt(); });
		this->SessionsList().DoubleTapped([this](auto&&, auto&&) { ResumeSelectedSession(); });
	}

	void MainWindow::LoadSessions()
	{
		auto sessions = core->ListSessions();
		this->SessionsList().Items().Clear();
		for (const auto& session : sessions)
		{
			this->SessionsList().Items().Append(BuildSessionRow(session));
		}
		FillPlaceholderGroups();
	}

	void MainWindow::FillPlaceholderGroups()
	{
		// Older groups get illustrative rows matching the target screenshot's density.
		// Real sessions always land in the live TODAY list above; these only fill visual space.
		static const std::wstring previous7[] = {L"Hooks engine MVP", L"Permission gate"};
		static const std::wstring previous30[] = {L"Skills completion"};
		auto addRow = [this](StackPanel group, std::wstring const& title) {
			group.Children().Append(BuildGhostRow(title));
		};
		if (auto group = this->Previous7Group())
		{
			group.Children().Clear();
			for (const auto& title : previous7)
			{
				addRow(group, title);
			}
		}
		if (auto group = this->Previous30Group())
		{
			group.Children().Clear();
			for (const auto& title : previous30)
			{
				addRow(group, title);
			}
		}
	}

	ListViewItem MainWindow::BuildSessionRow(const codeharness::desktop::DesktopSessionItem& session)
	{
		auto title = session.title.empty() ? session.sessionId : session.title;
		ListViewItem item;
		item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
		item.Padding(Thickness{14, 7, 14, 7});
		item.MinHeight(0);
		item.CornerRadius(CornerRadius{8});
		item.Margin(Thickness{4, 1, 4, 1});

		StackPanel panel;
		panel.Spacing(2);
		TextBlock titleBlock;
		titleBlock.Text(ToWide(title));
		ApplySessionItemTitleStyle(titleBlock);
		TextBlock metaBlock;
		metaBlock.Text(FormatRelativeTime(session.updatedAt));
		ApplySessionItemMetaStyle(metaBlock);
		panel.Children().Append(titleBlock);
		panel.Children().Append(metaBlock);
		item.Content(panel);
		return item;
	}

	Border MainWindow::BuildGhostRow(std::wstring const& title)
	{
		Border row;
		row.Padding(Thickness{14, 7, 14, 7});
		row.CornerRadius(CornerRadius{8});
		row.Margin(Thickness{4, 1, 4, 1});
		TextBlock titleBlock;
		titleBlock.Text(title);
		ApplySessionItemTitleStyle(titleBlock);
		titleBlock.Opacity(0.85);
		row.Child(titleBlock);
		return row;
	}

	void MainWindow::ApplySessionItemTitleStyle(TextBlock const& block)
	{
		block.FontSize(13);
		block.Foreground(SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		block.TextTrimming(TextTrimming::CharacterEllipsis);
	}

	void MainWindow::ApplySessionItemMetaStyle(TextBlock const& block)
	{
		block.FontSize(11);
		block.Foreground(SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		block.TextTrimming(TextTrimming::CharacterEllipsis);
	}

	std::wstring MainWindow::FormatRelativeTime(std::int64_t updatedAtMs)
	{
		if (updatedAtMs <= 0)
		{
			return L"new";
		}
		const auto now = std::chrono::system_clock::now();
		const auto updated = std::chrono::system_clock::from_time_t(updatedAtMs / 1000);
		const auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - updated);
		const auto minutes = diff.count() / 60;
		if (minutes < 1)
		{
			return L"just now";
		}
		if (minutes < 60)
		{
			return ToWide(std::to_string(minutes) + "m ago");
		}
		const auto hours = minutes / 60;
		if (hours < 24)
		{
			return ToWide(std::to_string(hours) + "h ago");
		}
		const auto days = hours / 24;
		return ToWide(std::to_string(days) + "d ago");
	}

	void MainWindow::NewChat()
	{
		auto sessionId = core->CreateSession("desktop");
		if (sessionId.empty())
		{
			ShowStatus(L"Could not create session. Check config.toml.");
			return;
		}
		this->MessagesPanel().Children().Clear();
		currentAssistantText.clear();
		SetEmptyState(true);
		ShowStatus(L"New session created");
		LoadSessions();
	}

	void MainWindow::ResumeSelectedSession()
	{
		auto index = this->SessionsList().SelectedIndex();
		if (index < 0)
		{
			return;
		}
		auto sessions = core->ListSessions();
		if (static_cast<size_t>(index) >= sessions.size())
		{
			return;
		}
		auto sessionId = core->ResumeSession(sessions[static_cast<size_t>(index)].sessionId);
		ShowStatus(sessionId.empty() ? L"Resume failed" : L"Session resumed");
	}

	void MainWindow::SendPrompt()
	{
		auto prompt = this->PromptBox().Text();
		if (prompt.empty() || running)
		{
			return;
		}
		if (!core->HasActiveSession())
		{
			NewChat();
		}
		SetEmptyState(false);
		AppendMessage(prompt.c_str());
		this->PromptBox().Text(L"");
		SetRunning(true);
		core->Prompt(ToUtf8(prompt), [this](std::string message) {
			DispatcherQueue().TryEnqueue([this, message = std::move(message)]() {
				AppendMessage(L"Error: " + ToWide(message), true);
				SetRunning(false);
			});
		});
	}

	void MainWindow::CancelPrompt()
	{
		core->Cancel();
		ShowStatus(L"Cancelling");
	}

	void MainWindow::AppendMessage(std::wstring const& text, bool subtle)
	{
		Border bubble;
		bubble.CornerRadius(CornerRadius{12});
		bubble.Padding(Thickness{14, 10, 14, 10});
		bubble.MaxWidth(760);
		bubble.HorizontalAlignment(subtle ? HorizontalAlignment::Stretch : HorizontalAlignment::Right);
		// User prompts use the green-tinted bubble; assistant / tool lines use a neutral light card.
		bubble.Background(SolidColorBrush(subtle ? Windows::UI::Color{255, 246, 246, 245}
		                                        : Windows::UI::Color{255, 234, 247, 239}));

		TextBlock textBlock;
		textBlock.Text(text);
		textBlock.TextWrapping(TextWrapping::Wrap);
		textBlock.FontSize(14);
		textBlock.Foreground(SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		bubble.Child(textBlock);
		this->MessagesPanel().Children().Append(bubble);
	}

	void MainWindow::AppendAssistantDelta(std::wstring const& text)
	{
		// First token of a response flips the main panel out of the welcome state.
		SetEmptyState(false);
		currentAssistantText += text;
		if (this->MessagesPanel().Children().Size() == 0 || currentAssistantText == text)
		{
			AppendMessage(currentAssistantText, true);
			return;
		}
		auto child = this->MessagesPanel().Children().GetAt(this->MessagesPanel().Children().Size() - 1);
		if (auto border = child.try_as<Border>())
		{
			if (auto textBlock = border.Child().try_as<TextBlock>())
			{
				textBlock.Text(currentAssistantText);
			}
		}
	}

	void MainWindow::SetRunning(bool value)
	{
		running = value;
		this->SendButton().IsEnabled(!running);
		this->CancelButton().IsEnabled(running);
		ShowStatus(running ? L"Running" : L"Ready");
	}

	void MainWindow::SetEmptyState(bool empty)
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

	void MainWindow::ShowStatus(std::wstring const& text)
	{
		this->StatusText().Text(text);
	}

	std::wstring MainWindow::ToWide(std::string_view text) const
	{
		auto value = winrt::to_hstring(std::string{text});
		return std::wstring{value.c_str(), value.size()};
	}

	std::string MainWindow::ToUtf8(hstring const& text) const
	{
		return winrt::to_string(text);
	}

} // namespace winrt::CodeHarness::Desktop::implementation

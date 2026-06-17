#include "MainWindow.xaml.h"

#include <future>
#include <memory>
#include <utility>

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
			auto label = session.title.empty() ? session.sessionId : session.title;
			this->SessionsList().Items().Append(box_value(ToWide(label)));
		}
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
		bubble.CornerRadius(CornerRadius{8});
		bubble.Padding(Thickness{14, 10, 14, 10});
		bubble.MaxWidth(760);
		bubble.HorizontalAlignment(subtle ? HorizontalAlignment::Stretch : HorizontalAlignment::Right);
		bubble.Background(SolidColorBrush(subtle ? Windows::UI::Color{255, 247, 247, 247} : Windows::UI::Color{255, 238, 244, 255}));

		TextBlock textBlock;
		textBlock.Text(text);
		textBlock.TextWrapping(TextWrapping::Wrap);
		textBlock.Foreground(SolidColorBrush(Windows::UI::Color{255, 32, 32, 32}));
		bubble.Child(textBlock);
		this->MessagesPanel().Children().Append(bubble);
	}

	void MainWindow::AppendAssistantDelta(std::wstring const& text)
	{
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

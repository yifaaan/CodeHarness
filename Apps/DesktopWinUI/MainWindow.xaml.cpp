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

	namespace
	{
		SolidColorBrush Brush(uint8_t red, uint8_t green, uint8_t blue)
		{
			return SolidColorBrush(Windows::UI::Color{255, red, green, blue});
		}
	} // namespace

	MainWindow::MainWindow()
	{
		BuildContent();
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

		newChatButton.Click([this](auto&&, auto&&) { NewChat(); });
		sendButton.Click([this](auto&&, auto&&) { SendPrompt(); });
		cancelButton.Click([this](auto&&, auto&&) { CancelPrompt(); });
		sessionsList.DoubleTapped([this](auto&&, auto&&) { ResumeSelectedSession(); });
	}

	void MainWindow::BuildContent()
	{
		Grid root;
		root.Background(Brush(252, 252, 250));
		root.ColumnDefinitions().Append(ColumnDefinition());
		root.ColumnDefinitions().GetAt(0).Width(GridLength{286, GridUnitType::Pixel});
		root.ColumnDefinitions().Append(ColumnDefinition());

		Border sidebar;
		sidebar.Background(Brush(246, 246, 243));
		sidebar.BorderBrush(Brush(224, 224, 219));
		sidebar.BorderThickness(Thickness{0, 0, 1, 0});
		Grid::SetColumn(sidebar, 0);

		Grid sidebarGrid;
		sidebarGrid.Padding(Thickness{18, 20, 18, 16});
		sidebarGrid.RowDefinitions().Append(RowDefinition());
		sidebarGrid.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
		sidebarGrid.RowDefinitions().Append(RowDefinition());
		sidebarGrid.RowDefinitions().GetAt(1).Height(GridLength{1, GridUnitType::Star});
		sidebarGrid.RowDefinitions().Append(RowDefinition());
		sidebarGrid.RowDefinitions().GetAt(2).Height(GridLengthHelper::Auto());
		sidebar.Child(sidebarGrid);

		StackPanel nav;
		nav.Spacing(8);
		newChatButton = Button();
		newChatButton.Content(box_value(L"New chat"));
		nav.Children().Append(newChatButton);
		for (auto text : {L"Search", L"Skills", L"Plugins", L"Automations"})
		{
			Button button;
			button.Content(box_value(text));
			button.HorizontalAlignment(HorizontalAlignment::Stretch);
			nav.Children().Append(button);
		}
		sidebarGrid.Children().Append(nav);

		sessionsList = ListView();
		sessionsList.Margin(Thickness{0, 28, 0, 8});
		sessionsList.SelectionMode(ListViewSelectionMode::Single);
		Grid::SetRow(sessionsList, 1);
		sidebarGrid.Children().Append(sessionsList);

		TextBlock footer;
		footer.Text(L"Manual permission mode");
		footer.Foreground(Brush(103, 103, 96));
		Grid::SetRow(footer, 2);
		sidebarGrid.Children().Append(footer);
		root.Children().Append(sidebar);

		Grid main;
		main.Padding(Thickness{64, 36, 64, 36});
		main.RowDefinitions().Append(RowDefinition());
		main.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
		main.RowDefinitions().Append(RowDefinition());
		main.RowDefinitions().GetAt(1).Height(GridLength{1, GridUnitType::Star});
		main.RowDefinitions().Append(RowDefinition());
		main.RowDefinitions().GetAt(2).Height(GridLengthHelper::Auto());
		Grid::SetColumn(main, 1);

		StackPanel header;
		header.Spacing(8);
		header.HorizontalAlignment(HorizontalAlignment::Center);
		TextBlock title;
		title.Text(L"What should we do?");
		title.FontSize(30);
		title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
		title.HorizontalAlignment(HorizontalAlignment::Center);
		title.Foreground(Brush(34, 34, 30));
		header.Children().Append(title);
		statusText = TextBlock();
		statusText.Text(L"Ready");
		statusText.HorizontalAlignment(HorizontalAlignment::Center);
		statusText.Foreground(Brush(103, 103, 96));
		header.Children().Append(statusText);
		main.Children().Append(header);

		ScrollViewer scroller;
		scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
		scroller.Padding(Thickness{0, 36, 0, 24});
		messagesPanel = StackPanel();
		messagesPanel.Spacing(12);
		messagesPanel.MaxWidth(920);
		messagesPanel.HorizontalAlignment(HorizontalAlignment::Center);
		scroller.Content(messagesPanel);
		Grid::SetRow(scroller, 1);
		main.Children().Append(scroller);

		Border composer;
		composer.MaxWidth(920);
		composer.HorizontalAlignment(HorizontalAlignment::Center);
		composer.Background(Brush(255, 255, 255));
		composer.BorderBrush(Brush(224, 224, 219));
		composer.BorderThickness(Thickness{1});
		composer.CornerRadius(CornerRadius{20});
		composer.Padding(Thickness{14});
		Grid::SetRow(composer, 2);

		Grid composerGrid;
		composerGrid.RowDefinitions().Append(RowDefinition());
		composerGrid.RowDefinitions().GetAt(0).Height(GridLengthHelper::Auto());
		composerGrid.RowDefinitions().Append(RowDefinition());
		composerGrid.RowDefinitions().GetAt(1).Height(GridLengthHelper::Auto());
		promptBox = TextBox();
		promptBox.PlaceholderText(L"Ask CodeHarness anything. Type @ to mention files or plugins.");
		promptBox.AcceptsReturn(true);
		promptBox.TextWrapping(TextWrapping::Wrap);
		promptBox.MinHeight(72);
		composerGrid.Children().Append(promptBox);

		StackPanel actions;
		actions.Orientation(Orientation::Horizontal);
		actions.HorizontalAlignment(HorizontalAlignment::Right);
		actions.Spacing(8);
		actions.Margin(Thickness{0, 10, 0, 0});
		cancelButton = Button();
		cancelButton.Content(box_value(L"Cancel"));
		cancelButton.IsEnabled(false);
		sendButton = Button();
		sendButton.Content(box_value(L"Send"));
		actions.Children().Append(cancelButton);
		actions.Children().Append(sendButton);
		Grid::SetRow(actions, 1);
		composerGrid.Children().Append(actions);
		composer.Child(composerGrid);
		main.Children().Append(composer);

		root.Children().Append(main);
		Content(root);
	}

	void MainWindow::LoadSessions()
	{
		auto sessions = core->ListSessions();
		sessionsList.Items().Clear();
		for (const auto& session : sessions)
		{
			auto label = session.title.empty() ? session.sessionId : session.title;
			sessionsList.Items().Append(box_value(ToWide(label)));
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
		messagesPanel.Children().Clear();
		ShowStatus(L"New session created");
		LoadSessions();
	}

	void MainWindow::ResumeSelectedSession()
	{
		auto index = sessionsList.SelectedIndex();
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
		auto prompt = promptBox.Text();
		if (prompt.empty() || running)
		{
			return;
		}
		if (!core->HasActiveSession())
		{
			NewChat();
		}
		AppendMessage(prompt.c_str());
		promptBox.Text(L"");
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
		messagesPanel.Children().Append(bubble);
	}

	void MainWindow::AppendAssistantDelta(std::wstring const& text)
	{
		currentAssistantText += text;
		if (messagesPanel.Children().Size() == 0 || currentAssistantText == text)
		{
			AppendMessage(currentAssistantText, true);
			return;
		}
		auto child = messagesPanel.Children().GetAt(messagesPanel.Children().Size() - 1);
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
		sendButton.IsEnabled(!running);
		cancelButton.IsEnabled(running);
		ShowStatus(running ? L"Running" : L"Ready");
	}

	void MainWindow::ShowStatus(std::wstring const& text)
	{
		statusText.Text(text);
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

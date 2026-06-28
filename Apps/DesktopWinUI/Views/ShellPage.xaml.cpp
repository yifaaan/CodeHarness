#include "Views/ShellPage.xaml.h"
#include "Controls/Sidebar.xaml.h"
#include "Controls/ComposerBox.xaml.h"
#include "Views/ChatPage.xaml.h"

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <future>
#include <sstream>
#include <system_error>
#include <utility>

#include <winrt/base.h>
#include <winrt/CodeHarness.Desktop.Controls.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <nlohmann/json.hpp>

namespace winrt::CodeHarness::Desktop::Views::implementation
{

	using namespace winrt::Microsoft::UI::Xaml;
	using namespace winrt::Microsoft::UI::Xaml::Controls;

	namespace desktop_app = ::codeharness::desktop_app;
	namespace desktop = ::codeharness::desktop;
	namespace tools = ::codeharness::tools;
	namespace permission = ::codeharness::permission;

	namespace
	{
		constexpr auto kSidebarDefaultWidth = 240.0;
		constexpr auto kSidebarMinWidth = 184.0;
		constexpr auto kSidebarMaxWidth = 340.0;
		constexpr auto kWorkspaceDefaultWidth = 320.0;
		constexpr auto kWorkspaceMinWidth = 260.0;
		constexpr auto kWorkspaceMaxWidth = 460.0;
	}

	ShellPage::ShellPage()
	{
		this->InitializeComponent();
		m_core = std::make_unique<desktop_app::DesktopCoreService>();
	}

	void ShellPage::Initialize()
	{
		WireCoreCallbacks();
		WireSidebarCallbacks();
		WireChatCallbacks();
		SetWorkspaceTab(0);
		RefreshEnvironment();
		RefreshGitChanges();
		RefreshProgress();
		RefreshFiles();
		LoadSessions();
	}

	void ShellPage::FocusComposer()
	{
		this->Chat().FocusComposer();
	}

	void ShellPage::OnTopSessionMenuClick(winrt::Windows::Foundation::IInspectable const&,
	                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		auto flyout = MenuFlyout{};

		auto appendItem = [&](std::wstring_view label, std::wstring_view glyph, auto handler) {
			auto item = MenuFlyoutItem{};
			item.Text(winrt::hstring{ label });
			FontIcon icon;
			icon.Glyph(winrt::hstring{ glyph });
			item.Icon(icon);
			item.Click(handler);
			flyout.Items().Append(item);
			return item;
		};

		if (!m_activeSessionId.empty())
		{
			appendItem(L"\u5206\u5C4F", L"\uE8A7", [this](auto&&, auto&&) {
				AddSessionToSplit(m_activeSessionId, FindSessionTitle(m_activeSessionId));
			});
			appendItem(L"\u91CD\u547D\u540D", L"\uE70F", [this](auto&&, auto&&) {
				PromptRenameSession(m_activeSessionId);
			});
			appendItem(L"\u590D\u5236\u4F1A\u8BDD ID", L"\uE8C8", [this](auto&&, auto&&) {
				CopySessionId(m_activeSessionId);
			});
			flyout.Items().Append(MenuFlyoutSeparator{});
			auto deleteItem = appendItem(L"\u5220\u9664\u4F1A\u8BDD", L"\uE74D", [this](auto&&, auto&&) {
				ConfirmDeleteSession(m_activeSessionId);
			});
			deleteItem.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 178, 54, 54}));
		}
		else
		{
			appendItem(L"\u65B0\u5EFA\u4F1A\u8BDD", L"\uE710", [this](auto&&, auto&&) {
				NewChat();
			});
			appendItem(L"\u6062\u590D\u6700\u8FD1\u4F1A\u8BDD", L"\uE8D1", [this](auto&&, auto&&) {
				ResumeSession({});
			});
			flyout.Items().Append(MenuFlyoutSeparator{});
			appendItem(L"\u8BBE\u7F6E", L"\uE713", [this](auto&&, auto&&) {
				OpenSettings();
			});
		}

		flyout.ShowAt(this->TopSessionMenuButton());
	}

	void ShellPage::OnTopGitClick(winrt::Windows::Foundation::IInspectable const&,
	                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SetWorkspaceVisible(true);
		SetWorkspaceTab(0);
	}

	void ShellPage::OnTopFilesClick(winrt::Windows::Foundation::IInspectable const&,
	                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SetWorkspaceVisible(true);
		RefreshFiles();
		SetWorkspaceTab(2);
	}

	void ShellPage::OnSidebarToggleClick(winrt::Windows::Foundation::IInspectable const&,
	                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		m_sidebarVisible = !m_sidebarVisible;
		auto visibility = m_sidebarVisible ? Visibility::Visible : Visibility::Collapsed;
		this->SidebarFrame().Visibility(visibility);
		this->SidebarGutter().Visibility(visibility);
		this->SidebarColumn().Width(GridLength{m_sidebarVisible ? m_sidebarWidth : 0.0, GridUnitType::Pixel});
		this->SidebarGutterColumn().Width(GridLength{m_sidebarVisible ? 8.0 : 0.0, GridUnitType::Pixel});
	}

	void ShellPage::OnWorkspaceToggleClick(winrt::Windows::Foundation::IInspectable const&,
	                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SetWorkspaceVisible(!m_workspaceVisible);
	}

	void ShellPage::OnWorkspaceGitTabClick(winrt::Windows::Foundation::IInspectable const&,
	                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SetWorkspaceTab(0);
	}

	void ShellPage::OnWorkspaceProgressTabClick(winrt::Windows::Foundation::IInspectable const&,
	                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		SetWorkspaceTab(1);
	}

	void ShellPage::OnWorkspaceFilesTabClick(winrt::Windows::Foundation::IInspectable const&,
	                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		RefreshFiles();
		SetWorkspaceTab(2);
	}

	void ShellPage::OnFilesNewFileClick(winrt::Windows::Foundation::IInspectable const&,
	                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		BeginFilesNewItem(false);
	}

	void ShellPage::OnFilesNewFolderClick(winrt::Windows::Foundation::IInspectable const&,
	                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		BeginFilesNewItem(true);
	}

	void ShellPage::OnFilesCreateItemClick(winrt::Windows::Foundation::IInspectable const&,
	                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		CreateFilesNewItem();
	}

	void ShellPage::OnFilesCancelNewItemClick(winrt::Windows::Foundation::IInspectable const&,
	                                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		CancelFilesNewItem();
	}

	void ShellPage::OnFilesNewItemKeyDown(winrt::Windows::Foundation::IInspectable const&,
	                                      winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
	{
		if (args.Key() == winrt::Windows::System::VirtualKey::Enter)
		{
			args.Handled(true);
			CreateFilesNewItem();
		}
		else if (args.Key() == winrt::Windows::System::VirtualKey::Escape)
		{
			args.Handled(true);
			CancelFilesNewItem();
		}
	}

	void ShellPage::OnFilesRefreshClick(winrt::Windows::Foundation::IInspectable const&,
	                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		RefreshFiles();
	}

	void ShellPage::OnFilesSearchTextChanged(winrt::Windows::Foundation::IInspectable const&,
	                                         winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&)
	{
		RefreshFiles();
	}

	void ShellPage::OnSidebarGutterPointerPressed(winrt::Windows::Foundation::IInspectable const&,
	                                              winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (!m_sidebarVisible)
		{
			return;
		}
		m_resizingSidebar = true;
		m_resizeStartX = args.GetCurrentPoint(this->ShellRoot()).Position().X;
		this->SidebarGutter().CapturePointer(args.Pointer());
		args.Handled(true);
	}

	void ShellPage::OnSidebarGutterPointerMoved(winrt::Windows::Foundation::IInspectable const&,
	                                            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (!m_resizingSidebar)
		{
			return;
		}
		auto x = args.GetCurrentPoint(this->ShellRoot()).Position().X;
		auto delta = x - m_resizeStartX;
		m_resizeStartX = x;
		m_sidebarWidth = std::clamp(m_sidebarWidth + delta, kSidebarMinWidth, kSidebarMaxWidth);
		this->SidebarColumn().Width(GridLength{m_sidebarWidth, GridUnitType::Pixel});
		args.Handled(true);
	}

	void ShellPage::OnSidebarGutterPointerReleased(winrt::Windows::Foundation::IInspectable const&,
	                                               winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		FinishSidebarResize();
		args.Handled(true);
	}

	void ShellPage::OnSidebarGutterPointerCanceled(winrt::Windows::Foundation::IInspectable const&,
	                                               winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		FinishSidebarResize();
		args.Handled(true);
	}

	void ShellPage::OnSidebarGutterDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
	                                            winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
	{
		m_sidebarWidth = kSidebarDefaultWidth;
		if (m_sidebarVisible)
		{
			this->SidebarColumn().Width(GridLength{m_sidebarWidth, GridUnitType::Pixel});
		}
		args.Handled(true);
	}

	void ShellPage::OnWorkspaceGutterPointerPressed(winrt::Windows::Foundation::IInspectable const&,
	                                                winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (!m_workspaceVisible)
		{
			return;
		}
		m_resizingWorkspace = true;
		m_resizeStartX = args.GetCurrentPoint(this->ShellRoot()).Position().X;
		this->WorkspaceGutter().CapturePointer(args.Pointer());
		args.Handled(true);
	}

	void ShellPage::OnWorkspaceGutterPointerMoved(winrt::Windows::Foundation::IInspectable const&,
	                                              winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		if (!m_resizingWorkspace)
		{
			return;
		}
		auto x = args.GetCurrentPoint(this->ShellRoot()).Position().X;
		auto delta = x - m_resizeStartX;
		m_resizeStartX = x;
		m_workspaceWidth = std::clamp(m_workspaceWidth - delta, kWorkspaceMinWidth, kWorkspaceMaxWidth);
		this->WorkspaceColumn().Width(GridLength{m_workspaceWidth, GridUnitType::Pixel});
		args.Handled(true);
	}

	void ShellPage::OnWorkspaceGutterPointerReleased(winrt::Windows::Foundation::IInspectable const&,
	                                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		FinishWorkspaceResize();
		args.Handled(true);
	}

	void ShellPage::OnWorkspaceGutterPointerCanceled(winrt::Windows::Foundation::IInspectable const&,
	                                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args)
	{
		FinishWorkspaceResize();
		args.Handled(true);
	}

	void ShellPage::OnWorkspaceGutterDoubleTapped(winrt::Windows::Foundation::IInspectable const&,
	                                              winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args)
	{
		m_workspaceWidth = kWorkspaceDefaultWidth;
		if (m_workspaceVisible)
		{
			this->WorkspaceColumn().Width(GridLength{m_workspaceWidth, GridUnitType::Pixel});
		}
		args.Handled(true);
	}

	void ShellPage::OnSettingsClick(winrt::Windows::Foundation::IInspectable const&,
	                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
	{
		OpenSettings();
	}

	void ShellPage::WireCoreCallbacks()
	{
		auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
		auto chat = this->Chat();

			m_core->SetEventCallback([this, queue, chat](const desktop::DesktopEvent& event) {
				queue.TryEnqueue([this, chat, event]() {
					if (event.type == "loop")
					{
						auto loop = event.payload.value("loop_event", nlohmann::json::object());
						if (loop.contains("ThinkingDelta"))
						{
							// Accumulate reasoning text; rendered when the thinking block ends.
							m_currentThinking += ToWide(loop["ThinkingDelta"].value("text", ""));
						}
						else if (loop.contains("AssistantDelta"))
						{
							// The assistant's visible text has started: flush any pending
							// thinking block before appending the first text bubble.
							if (!m_currentThinking.empty())
							{
								chat.AppendThinkingBlock(winrt::hstring{ m_currentThinking }, 0);
								m_currentThinking.clear();
							}
							chat.AppendAssistantDelta(winrt::hstring{ ToWide(loop["AssistantDelta"].value("text", "")) });
						}
						else if (loop.contains("StepStarted"))
						{
							m_currentSteps = loop["StepStarted"].value("step", m_currentSteps);
							RefreshUsage();
						}
						else if (loop.contains("ToolCallStarted"))
						{
							// Flush any pending thinking block before the tool card.
							if (!m_currentThinking.empty())
							{
								chat.AppendThinkingBlock(winrt::hstring{ m_currentThinking }, 0);
								m_currentThinking.clear();
							}
							auto tool = loop["ToolCallStarted"];
							auto name = ToWide(tool.value("name", ""));
							chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ L"running\u2026" }, false, winrt::hstring{});
							// Collect git-related file changes for the Git panel.
							if (tool.contains("args"))
							{
								CollectGitChange(tool["args"]);
							}
						}
					else if (loop.contains("ToolResult"))
					{
						auto tool = loop["ToolResult"];
						auto status = tool.value("status", "");
						auto detail = status == "error" ? L"failed" : L"completed";
						auto name = ToWide(tool.value("name", ""));
						bool isError = (status == "error");
						auto output = ToWide(tool.value("output", ""));
						chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ detail }, isError, winrt::hstring{ output });
						// Record the completed step for the right-panel Progress section.
						if (!isError)
						{
							m_completedSteps.insert(m_completedSteps.begin(), name);
							if (m_completedSteps.size() > 50) m_completedSteps.resize(50);
							RefreshProgress();
						}
						RefreshGitChanges();
					}
					else if (loop.contains("PermissionDenied"))
					{
						auto permission = loop["PermissionDenied"];
						auto name = ToWide(permission.value("name", ""));
						chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ L"denied" }, true, winrt::hstring{});
					}
				}
				else if (event.type == "turn_started")
				{
					m_running = true;
					m_currentSteps = 0;
					m_currentThinking.clear();
					chat.SetRunning(true);
					this->TopStatusText().Text(L"运行中");
					RefreshUsage();
				}
				else if (event.type == "turn_ended")
				{
					m_running = false;
					chat.SetRunning(false);
					this->TopStatusText().Text(L"就绪");
					// Flush any trailing thinking block (e.g. a turn that only produced
					// reasoning and no visible assistant text).
					if (!m_currentThinking.empty())
					{
						chat.AppendThinkingBlock(winrt::hstring{ m_currentThinking }, 0);
						m_currentThinking.clear();
					}
					// Upgrade the streamed assistant bubble to rendered markdown.
					chat.AppendAssistantComplete();
					auto result = event.payload.value("result", nlohmann::json::object());
					auto usage = result.value("usage", nlohmann::json::object());
					std::int64_t turnTokens = 0;
					turnTokens += usage.value("input_other", static_cast<std::int64_t>(0));
					turnTokens += usage.value("output", static_cast<std::int64_t>(0));
					turnTokens += usage.value("input_cache_read", static_cast<std::int64_t>(0));
					turnTokens += usage.value("input_cache_creation", static_cast<std::int64_t>(0));
					m_totalTokens += turnTokens;
					m_currentSteps = 0;
					// Surface the cumulative token count in the toolbar.
					auto usageText = std::wstring{ L"累计 " } + FormatTokenCount(m_totalTokens);
					chat.SetUsage(winrt::hstring{ usageText });
					this->TopUsageText().Text(winrt::hstring{ usageText });
				}
				else if (event.type == "error")
				{
					chat.AppendStatusMessage(winrt::hstring{ L"\u9519\u8BEF: " + ToWide(event.payload.value("message", "")) });
					m_running = false;
					chat.SetRunning(false);
					this->TopStatusText().Text(L"错误");
				}
			});
		});

		m_core->SetApprovalCallback([this, queue](const desktop::DesktopPermissionRequest& request) {
			auto promise = std::make_shared<std::promise<bool>>();
			auto future = promise->get_future();
			queue.TryEnqueue([this, request, promise]() {
				ContentDialog dialog;
				dialog.XamlRoot(this->XamlRoot());
				dialog.Title(box_value(L"\u6743\u9650\u8BF7\u6C42"));

				StackPanel panel;
				panel.Spacing(12);
				panel.MaxWidth(560);

				Grid header;
				{
					ColumnDefinition iconCol;
					iconCol.Width(GridLength{34, GridUnitType::Pixel});
					header.ColumnDefinitions().Append(iconCol);
				}
				{
					ColumnDefinition textCol;
					textCol.Width(GridLength{1, GridUnitType::Star});
					header.ColumnDefinitions().Append(textCol);
				}
				header.ColumnSpacing(10);

				Border iconFrame;
				winrt::Microsoft::UI::Xaml::CornerRadius iconRadius{10, 10, 10, 10};
				iconFrame.CornerRadius(iconRadius);
				iconFrame.Width(30);
				iconFrame.Height(30);
				iconFrame.Background(Media::SolidColorBrush(Windows::UI::Color{255, 245, 245, 244}));
				TextBlock icon;
				icon.Text(L"\uE756");
				icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
				icon.FontSize(14);
				icon.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 36, 36, 36}));
				icon.HorizontalAlignment(HorizontalAlignment::Center);
				icon.VerticalAlignment(VerticalAlignment::Center);
				iconFrame.Child(icon);
				Grid::SetColumn(iconFrame, 0);
				header.Children().Append(iconFrame);

				StackPanel titleStack;
				titleStack.Spacing(2);
				TextBlock title;
				title.Text(winrt::hstring{ ToWide(request.toolName) });
				title.FontSize(14);
				title.FontWeight(Windows::UI::Text::FontWeight{600});
				title.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 36, 36, 36}));
				titleStack.Children().Append(title);

				TextBlock subtitle;
				subtitle.Text(L"\u8BF7\u786E\u8BA4\u662F\u5426\u5141\u8BB8\u672C\u6B21\u5DE5\u5177\u6267\u884C");
				subtitle.FontSize(12);
				subtitle.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 133, 133, 125}));
				titleStack.Children().Append(subtitle);
				Grid::SetColumn(titleStack, 1);
				header.Children().Append(titleStack);
				panel.Children().Append(header);

				TextBlock description;
				description.Text(winrt::hstring{ ToWide(request.description) });
				description.FontSize(12);
				description.TextWrapping(TextWrapping::Wrap);
				description.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 88, 88, 82}));
				panel.Children().Append(description);

				TextBlock inputLabel;
				inputLabel.Text(L"\u5DE5\u5177\u8F93\u5165");
				inputLabel.FontSize(11);
				inputLabel.FontWeight(Windows::UI::Text::FontWeight{600});
				inputLabel.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 88, 88, 82}));
				panel.Children().Append(inputLabel);

				auto argsText = request.args.is_null() || request.args.empty()
					? std::wstring{L"{}"}
					: ToWide(request.args.dump(2));
				if (argsText.size() > 2400)
				{
					argsText = argsText.substr(0, 2400) + L"\n...";
				}

				Border argsFrame;
				winrt::Microsoft::UI::Xaml::CornerRadius argsRadius{10, 10, 10, 10};
				argsFrame.CornerRadius(argsRadius);
				argsFrame.Padding(Thickness{10, 8, 10, 8});
				argsFrame.Background(Media::SolidColorBrush(Windows::UI::Color{255, 245, 245, 244}));
				argsFrame.BorderBrush(Media::SolidColorBrush(Windows::UI::Color{255, 229, 229, 226}));
				argsFrame.BorderThickness(Thickness{1, 1, 1, 1});
				TextBlock argsBlock;
				argsBlock.Text(winrt::hstring{ argsText });
				argsBlock.FontFamily(Media::FontFamily(L"Consolas"));
				argsBlock.FontSize(11);
				argsBlock.TextWrapping(TextWrapping::Wrap);
				argsBlock.IsTextSelectionEnabled(true);
				argsBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 60, 60, 56}));
				argsFrame.Child(argsBlock);
				panel.Children().Append(argsFrame);

				Border hintFrame;
				winrt::Microsoft::UI::Xaml::CornerRadius hintRadius{10, 10, 10, 10};
				hintFrame.CornerRadius(hintRadius);
				hintFrame.Padding(Thickness{10, 8, 10, 8});
				hintFrame.Background(Media::SolidColorBrush(Windows::UI::Color{255, 255, 251, 235}));
				Grid hintGrid;
				{
					ColumnDefinition hintIconCol;
					hintIconCol.Width(GridLength{18, GridUnitType::Pixel});
					hintGrid.ColumnDefinitions().Append(hintIconCol);
				}
				{
					ColumnDefinition hintTextCol;
					hintTextCol.Width(GridLength{1, GridUnitType::Star});
					hintGrid.ColumnDefinitions().Append(hintTextCol);
				}
				hintGrid.ColumnSpacing(8);
				TextBlock hintIcon;
				hintIcon.Text(L"\uE7BA");
				hintIcon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
				hintIcon.FontSize(13);
				hintIcon.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 180, 83, 9}));
				hintIcon.VerticalAlignment(VerticalAlignment::Top);
				Grid::SetColumn(hintIcon, 0);
				hintGrid.Children().Append(hintIcon);
				TextBlock hint;
				hint.Text(L"\u5141\u8BB8\u540E\uFF0CCodeHarness \u5C06\u7ACB\u5373\u6267\u884C\u8BE5\u5DE5\u5177\uFF1B\u62D2\u7EDD\u5219\u8DF3\u8FC7\u672C\u6B21\u8C03\u7528\u3002");
				hint.FontSize(11);
				hint.TextWrapping(TextWrapping::Wrap);
				hint.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 146, 64, 14}));
				Grid::SetColumn(hint, 1);
				hintGrid.Children().Append(hint);
				hintFrame.Child(hintGrid);
				panel.Children().Append(hintFrame);

				dialog.Content(panel);
				dialog.PrimaryButtonText(L"\u5141\u8BB8");
				dialog.CloseButtonText(L"\u62D2\u7EDD");
				dialog.DefaultButton(ContentDialogButton::Primary);
				auto op = dialog.ShowAsync();
				op.Completed([promise](auto const& async, auto) {
					promise->set_value(async.GetResults() == ContentDialogResult::Primary);
				});
			});
			return future.get();
		});

		m_core->SetQuestionCallback([this, queue](const tools::QuestionRequest& request) {
			auto promise = std::make_shared<std::promise<std::string>>();
			auto future = promise->get_future();
			queue.TryEnqueue([this, request, promise]() {
				ContentDialog dialog;
				dialog.XamlRoot(this->XamlRoot());
				dialog.Title(box_value(L"CodeHarness \u63D0\u95EE"));
				dialog.Content(box_value(winrt::hstring{ ToWide(request.question) }));
				dialog.PrimaryButtonText(request.options.empty() ? L"\u786E\u5B9A" : winrt::hstring{ ToWide(request.options.front()) });
				dialog.CloseButtonText(L"\u53D6\u6D88");
				auto op = dialog.ShowAsync();
				op.Completed([choice = request.options.empty() ? std::string{} : request.options.front(),
				              promise](auto const& async, auto) {
					promise->set_value(async.GetResults() == ContentDialogResult::Primary ? choice : std::string{});
				});
			});
			return future.get();
		});
	}

	void ShellPage::AddSessionToSplit(std::wstring sessionId, std::wstring titleHint)
	{
		if (sessionId.empty())
		{
			return;
		}

		auto sidebarImpl = this->SidebarControl()
		                       .try_as<winrt::CodeHarness::Desktop::Controls::implementation::Sidebar>();
		if (!sidebarImpl)
		{
			return;
		}

		auto targetTitle = titleHint.empty() ? FindSessionTitle(sessionId) : std::move(titleHint);
		std::wstring currentTitle;
		if (!m_activeSessionId.empty())
		{
			currentTitle = FindSessionTitle(m_activeSessionId);
		}

		if (!m_activeSessionId.empty() && m_activeSessionId != sessionId)
		{
			sidebarImpl->AddSplitSession(
				m_activeSessionId,
				currentTitle.empty() ? m_activeSessionId : currentTitle);
		}
		sidebarImpl->AddSplitSession(sessionId, targetTitle.empty() ? sessionId : targetTitle);
		ResumeSession(sessionId);
		this->TopStatusText().Text(L"\u5DF2\u52A0\u5165\u5206\u5C4F");
	}

	void ShellPage::RenameSession(std::wstring sessionId, std::wstring title)
	{
		if (sessionId.empty() || title.empty())
		{
			return;
		}

		auto sessionKey = winrt::hstring{ sessionId.c_str(), static_cast<uint32_t>(sessionId.size()) };
		auto nextTitle = winrt::hstring{ title.c_str(), static_cast<uint32_t>(title.size()) };
		if (!m_core->RenameSession(winrt::to_string(sessionKey), winrt::to_string(nextTitle)))
		{
			this->Chat().SetStatus(L"\u91CD\u547D\u540D\u5931\u8D25");
			return;
		}

		LoadSessions();
		this->SidebarControl().SetActiveSession(sessionKey);
		this->TopStatusText().Text(L"\u5DF2\u91CD\u547D\u540D");
		if (sessionId == m_activeSessionId)
		{
			this->Chat().SetPageTitle(nextTitle);
			this->TopTitleText().Text(nextTitle);
		}
	}

	void ShellPage::PromptRenameSession(std::wstring sessionId)
	{
		if (sessionId.empty())
		{
			return;
		}

		StackPanel panel;
		panel.Spacing(8);
		TextBlock hint;
		hint.Text(L"\u4E3A\u5F53\u524D\u4F1A\u8BDD\u8F93\u5165\u4E00\u4E2A\u65B0\u6807\u9898\u3002");
		hint.FontSize(12);
		hint.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 133, 133, 125}));
		panel.Children().Append(hint);

		TextBox input;
		auto title = FindSessionTitle(sessionId);
		input.Text(winrt::hstring{ title.empty() ? sessionId : title });
		input.MinWidth(320);
		input.SelectAll();
		panel.Children().Append(input);

		ContentDialog dialog;
		dialog.XamlRoot(this->XamlRoot());
		dialog.Title(box_value(L"\u91CD\u547D\u540D\u4F1A\u8BDD"));
		dialog.Content(panel);
		dialog.PrimaryButtonText(L"\u4FDD\u5B58");
		dialog.CloseButtonText(L"\u53D6\u6D88");
		dialog.DefaultButton(ContentDialogButton::Primary);
		auto op = dialog.ShowAsync();
		op.Completed([this, sessionId, input](auto const& async, auto) {
			if (async.GetResults() != ContentDialogResult::Primary)
			{
				return;
			}
			auto value = input.Text();
			auto nextTitle = std::wstring{ value.c_str(), value.size() };
			RenameSession(sessionId, std::move(nextTitle));
		});
	}

	void ShellPage::CopySessionId(std::wstring const& sessionId)
	{
		if (sessionId.empty())
		{
			return;
		}

		winrt::Windows::ApplicationModel::DataTransfer::DataPackage data;
		data.SetText(winrt::hstring{ sessionId });
		winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(data);
		this->TopStatusText().Text(L"\u5DF2\u590D\u5236\u4F1A\u8BDD ID");
	}

	void ShellPage::ConfirmDeleteSession(std::wstring sessionId)
	{
		if (sessionId.empty())
		{
			return;
		}

		auto title = FindSessionTitle(sessionId);
		if (title.empty())
		{
			title = sessionId;
		}

		StackPanel panel;
		panel.Spacing(8);
		TextBlock message;
		message.Text(winrt::hstring{ L"\u786E\u5B9A\u8981\u5220\u9664\u201C" + title + L"\u201D\u5417\uFF1F" });
		message.FontSize(14);
		message.TextWrapping(TextWrapping::Wrap);
		message.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 36, 36, 36}));
		panel.Children().Append(message);

		TextBlock hint;
		hint.Text(L"\u8FD9\u4F1A\u79FB\u9664\u8BE5\u4F1A\u8BDD\u7684\u672C\u5730\u8BB0\u5F55\u3002");
		hint.FontSize(12);
		hint.TextWrapping(TextWrapping::Wrap);
		hint.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 133, 133, 125}));
		panel.Children().Append(hint);

		ContentDialog dialog;
		dialog.XamlRoot(this->XamlRoot());
		dialog.Title(box_value(L"\u5220\u9664\u4F1A\u8BDD"));
		dialog.Content(panel);
		dialog.PrimaryButtonText(L"\u5220\u9664");
		dialog.CloseButtonText(L"\u53D6\u6D88");
		dialog.DefaultButton(ContentDialogButton::Close);
		auto op = dialog.ShowAsync();
		op.Completed([this, sessionId](auto const& async, auto) {
			if (async.GetResults() != ContentDialogResult::Primary)
			{
				return;
			}
			if (m_running && sessionId == m_activeSessionId)
			{
				this->Chat().SetStatus(L"\u8BF7\u5148\u505C\u6B62\u5F53\u524D\u8FD0\u884C\u540E\u518D\u5220\u9664\u4F1A\u8BDD");
				return;
			}

			auto sessionKey = winrt::hstring{ sessionId.c_str(), static_cast<uint32_t>(sessionId.size()) };
			if (!m_core->RemoveSession(winrt::to_string(sessionKey)))
			{
				this->Chat().SetStatus(L"\u5220\u9664\u4F1A\u8BDD\u5931\u8D25");
				return;
			}

			auto deletedActive = sessionId == m_activeSessionId;
			if (deletedActive)
			{
				m_activeSessionId.clear();
				this->Chat().ResetTranscript();
				this->Chat().SetEmptyState(true);
				this->Chat().SetPageTitle(L"\u65B0\u5BF9\u8BDD");
				this->Chat().SetStatus(L"\u5DF2\u5220\u9664\u4F1A\u8BDD");
				this->TopTitleText().Text(L"\u65B0\u5BF9\u8BDD");
				this->TopUsageText().Text(L"");
			}
			LoadSessions();
			this->TopStatusText().Text(L"\u5DF2\u5220\u9664\u4F1A\u8BDD");
		});
	}

	void ShellPage::WireSidebarCallbacks()
	{
		auto sidebar = this->SidebarControl()
		                   .try_as<winrt::CodeHarness::Desktop::Controls::implementation::Sidebar>();
		if (!sidebar)
		{
			return;
		}
		sidebar->OnNewChat([this]() { NewChat(); });
		sidebar->OnResume([this](std::wstring titleHint) { ResumeSession(std::move(titleHint)); });
		sidebar->OnAddToSplit([this](std::wstring sessionId, std::wstring title) {
			AddSessionToSplit(std::move(sessionId), std::move(title));
		});
		sidebar->OnFork([this](std::wstring sessionId) {
			if (sessionId.empty())
			{
				return;
			}
			auto sessionKey = winrt::hstring{ sessionId.c_str(), static_cast<uint32_t>(sessionId.size()) };
			auto forked = m_core->ForkSession(winrt::to_string(sessionKey));
			if (forked.empty())
			{
				this->Chat().SetStatus(L"\u5206\u53C9\u4F1A\u8BDD\u5931\u8D25");
				return;
			}

			LoadSessions();
			ResumeSession(ToWide(forked));
			this->TopStatusText().Text(L"\u5DF2\u521B\u5EFA\u5206\u53C9\u4F1A\u8BDD");
		});
		sidebar->OnRename([this](std::wstring sessionId, std::wstring title) {
			RenameSession(std::move(sessionId), std::move(title));
		});
		sidebar->OnDelete([this](std::wstring sessionId) {
			ConfirmDeleteSession(std::move(sessionId));
		});
		sidebar->OnOpenSettings([this]() { OpenSettings(); });
	}

	void ShellPage::WireChatCallbacks()
	{
		auto chat = this->Chat().try_as<winrt::CodeHarness::Desktop::Views::implementation::ChatPage>();
		if (!chat)
		{
			return;
		}
		chat->OnSubmit([this](std::wstring text) { SendText(std::move(text)); });
		chat->OnCancel([this]() { CancelPrompt(); });

		// Wire the composer: inject the model list from the backend and forward
		// model/thinking changes back to the core service.
		auto composerImpl = chat->Composer().try_as<winrt::CodeHarness::Desktop::Controls::implementation::ComposerBox>();
		if (composerImpl)
		{
			std::vector<std::wstring> wideModels;
			for (const auto& model : m_core->ListModels())
			{
				wideModels.push_back(ToWide(model));
			}
			if (!wideModels.empty())
			{
				composerImpl->SetAvailableModels(wideModels);
			}
			composerImpl->OnModelSelected([this](std::wstring model) {
				std::string utf8 = winrt::to_string(winrt::hstring{ model.c_str(), static_cast<uint32_t>(model.size()) });
				m_core->SetModel(utf8);
			});
			composerImpl->OnThinkingToggled([this](bool enabled) {
				m_core->SetThinking(enabled);
			});
		}
	}

	void ShellPage::LoadSessions()
	{
		auto sessions = m_core->ListSessions();
		auto titles = winrt::single_threaded_vector<winrt::hstring>();
		auto ids = winrt::single_threaded_vector<winrt::hstring>();
		auto timestamps = winrt::single_threaded_vector<std::int64_t>();
		for (const auto& session : sessions)
		{
			auto title = session.title.empty() ? session.sessionId : session.title;
			ids.Append(winrt::hstring{ ToWide(session.sessionId) });
			titles.Append(winrt::hstring{ ToWide(title) });
			// updatedAt (fallback to createdAt, then now) drives the time-bucketing.
			std::int64_t ts = session.updatedAt != 0 ? session.updatedAt
				: (session.createdAt != 0 ? session.createdAt : 0);
			timestamps.Append(ts);
		}
		this->SidebarControl().SetSessionItems(titles.GetView(), ids.GetView(), timestamps.GetView());
		if (!m_activeSessionId.empty())
		{
			this->SidebarControl().SetActiveSession(
				winrt::hstring{ m_activeSessionId.c_str(), static_cast<uint32_t>(m_activeSessionId.size()) });
		}
	}

	void ShellPage::SendText(std::wstring text)
	{
		if (text.empty() || m_running)
		{
			return;
		}
		if (!m_core->HasActiveSession())
		{
			NewChat();
		}
		auto chat = this->Chat();
		chat.SetEmptyState(false);
		chat.AppendUserMessage(winrt::hstring{ text.c_str(), static_cast<uint32_t>(text.size()) });
		chat.SetRunning(true);
		m_running = true;
		winrt::hstring wide{ text.c_str(), static_cast<uint32_t>(text.size()) };
		std::string utf8 = winrt::to_string(wide);
		m_core->Prompt(std::move(utf8), [this, chat](std::string message) {
			auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
			if (!queue)
			{
				return;
			}
			queue.TryEnqueue([this, chat, message = std::move(message)]() {
				chat.AppendStatusMessage(winrt::hstring{ L"\u9519\u8BEF: " + ToWide(message) });
				chat.SetRunning(false);
				m_running = false;
			});
		});
	}

	void ShellPage::NewChat()
	{
		auto sessionId = m_core->CreateSession(winrt::to_string(winrt::hstring{ L"\u65B0\u5BF9\u8BDD" }));
		if (sessionId.empty())
		{
			this->Chat().SetStatus(winrt::hstring{ L"\u65E0\u6CD5\u521B\u5EFA\u4F1A\u8BDD\uFF0C\u8BF7\u68C0\u67E5 config.toml" });
			return;
		}
		m_activeSessionId = ToWide(sessionId);
		this->Chat().ResetTranscript();
		this->Chat().SetStatus(winrt::hstring{ L"\u65B0\u5EFA\u4F1A\u8BDD\u5DF2\u521B\u5EFA" });
		this->TopTitleText().Text(L"新对话");
		this->TopStatusText().Text(L"就绪");
		this->TopUsageText().Text(L"");
		// Reset accumulated state for the fresh session.
		m_totalTokens = 0;
		m_currentSteps = 0;
		m_gitChanges.clear();
		m_completedSteps.clear();
		RefreshEnvironment();
		RefreshUsage();
		RefreshGitChanges();
		RefreshProgress();
		LoadSessions();
		this->SidebarControl().SetActiveSession(
			winrt::hstring{ m_activeSessionId.c_str(), static_cast<uint32_t>(m_activeSessionId.size()) });
	}

	void ShellPage::ResumeSession(std::wstring titleHint)
	{
		auto sessions = m_core->ListSessions();
		std::string targetId;
		std::string targetTitle;
		if (!titleHint.empty())
		{
			for (const auto& session : sessions)
			{
				if (ToWide(session.sessionId) == titleHint)
				{
					targetId = session.sessionId;
					targetTitle = session.title.empty() ? session.sessionId : session.title;
					break;
				}
			}
			if (targetId.empty())
			{
				for (const auto& session : sessions)
				{
					auto title = session.title.empty() ? session.sessionId : session.title;
					if (ToWide(title) == titleHint)
					{
						targetId = session.sessionId;
						targetTitle = title;
						break;
					}
				}
			}
		}
		if (targetId.empty() && !sessions.empty())
		{
			targetId = sessions.front().sessionId;
			targetTitle = sessions.front().title.empty() ? sessions.front().sessionId : sessions.front().title;
		}
		if (targetId.empty())
		{
			return;
		}
		auto resumed = m_core->ResumeSession(targetId);
		if (resumed.empty())
		{
			this->Chat().SetStatus(L"\u6062\u590D\u5931\u8D25");
			return;
		}
		m_activeSessionId = ToWide(resumed);
		this->Chat().SetStatus(L"\u4F1A\u8BDD\u5DF2\u6062\u590D");
		// Reflect the resumed session's title + current branch in the UI.
		if (!targetTitle.empty())
		{
			this->Chat().SetPageTitle(winrt::hstring{ ToWide(targetTitle) });
			this->TopTitleText().Text(winrt::hstring{ ToWide(targetTitle) });
		}
		LoadSessions();
		this->SidebarControl().SetActiveSession(
			winrt::hstring{ m_activeSessionId.c_str(), static_cast<uint32_t>(m_activeSessionId.size()) });
		RefreshEnvironment();
	}

	void ShellPage::CancelPrompt()
	{
		m_core->Cancel();
		this->Chat().SetStatus(winrt::hstring{ L"\u6B63\u5728\u53D6\u6D88\u2026" });
		this->TopStatusText().Text(L"正在取消…");
	}

	void ShellPage::OpenSettings()
	{
		StackPanel panel;
		panel.Spacing(12);
		panel.Padding(Thickness{0, 8, 0, 0});

		// Workdir (read-only).
		auto workdir = m_core->CurrentWorkdir();
		TextBlock workdirBlock;
		workdirBlock.Text(L"\u5DE5\u4F5C\u533A: " + ToWide(workdir));
		workdirBlock.FontSize(14);
		workdirBlock.TextWrapping(TextWrapping::Wrap);
		workdirBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(workdirBlock);

		// Available models from the backend, with the active one marked.
		auto models = m_core->ListModels();
		std::wstring modelLine = L"\u53EF\u7528\u6A21\u578B: ";
		if (models.empty())
		{
			modelLine += L"\uFF08\u672A\u914D\u7F6E\uFF09";
		}
		else
		{
			for (size_t i = 0; i < models.size(); ++i)
			{
				if (i > 0) modelLine += L"\u3001";
				modelLine += ToWide(models[i]);
			}
		}
		TextBlock modelBlock;
		modelBlock.Text(modelLine);
		modelBlock.FontSize(14);
		modelBlock.TextWrapping(TextWrapping::Wrap);
		modelBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(modelBlock);

		// Permission mode (the desktop always runs in Manual today; be honest).
		TextBlock perm;
		perm.Text(L"\u6743\u9650\u6A21\u5F0F: \u624B\u52A8\u5BA1\u6279\u6BCF\u4E2A\u5DE5\u5177");
		perm.FontSize(14);
		perm.TextWrapping(TextWrapping::Wrap);
		perm.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(perm);

		TextBlock hint;
		hint.Text(L"\u7F16\u8F91 ~/.codeharness/config.toml \u4EE5\u66F4\u6539\u6A21\u578B\u3001\u63D0\u4F9B\u5546\u3001\u94A9\u5B50\u548C\u6280\u80FD\u3002");
		hint.FontSize(12);
		hint.TextWrapping(TextWrapping::Wrap);
		hint.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		panel.Children().Append(hint);

		ContentDialog dialog;
		dialog.XamlRoot(this->XamlRoot());
		dialog.Title(box_value(L"\u8BBE\u7F6E"));
		dialog.Content(panel);
		dialog.CloseButtonText(L"\u5B8C\u6210");
		dialog.DefaultButton(ContentDialogButton::Close);
		(void)dialog.ShowAsync();
	}

	void ShellPage::SetWorkspaceVisible(bool visible)
	{
		m_workspaceVisible = visible;
		auto visibility = visible ? Visibility::Visible : Visibility::Collapsed;
		this->WorkspaceFrame().Visibility(visibility);
		this->WorkspaceGutter().Visibility(visibility);
		this->WorkspaceColumn().Width(GridLength{visible ? m_workspaceWidth : 0.0, GridUnitType::Pixel});
		this->WorkspaceGutterColumn().Width(GridLength{visible ? 8.0 : 0.0, GridUnitType::Pixel});
		this->WorkspaceToggleButton().Style(Application::Current().Resources()
		                                        .Lookup(winrt::box_value(visible
		                                                                  ? L"TopBarActiveIconButtonStyle"
		                                                                  : L"TopBarIconButtonStyle"))
		                                        .try_as<winrt::Microsoft::UI::Xaml::Style>());
	}

	void ShellPage::SetWorkspaceTab(std::int32_t tab)
	{
		m_workspaceTab = tab;
		auto activeStyle = Application::Current().Resources()
		                       .Lookup(winrt::box_value(L"WorkspaceTabActiveButtonStyle"))
		                       .try_as<winrt::Microsoft::UI::Xaml::Style>();
		auto inactiveStyle = Application::Current().Resources()
		                         .Lookup(winrt::box_value(L"WorkspaceTabButtonStyle"))
		                         .try_as<winrt::Microsoft::UI::Xaml::Style>();

		this->WorkspaceGitTabButton().Style(tab == 0 ? activeStyle : inactiveStyle);
		this->WorkspaceProgressTabButton().Style(tab == 1 ? activeStyle : inactiveStyle);
		this->WorkspaceFilesTabButton().Style(tab == 2 ? activeStyle : inactiveStyle);

		this->WorkspaceGitTabContent().Visibility(tab == 0 ? Visibility::Visible : Visibility::Collapsed);
		this->WorkspaceProgressTabContent().Visibility(tab == 1 ? Visibility::Visible : Visibility::Collapsed);
		this->WorkspaceFilesTabContent().Visibility(tab == 2 ? Visibility::Visible : Visibility::Collapsed);
	}

	void ShellPage::FinishSidebarResize()
	{
		if (!m_resizingSidebar)
		{
			return;
		}
		m_resizingSidebar = false;
		this->SidebarGutter().ReleasePointerCaptures();
	}

	void ShellPage::FinishWorkspaceResize()
	{
		if (!m_resizingWorkspace)
		{
			return;
		}
		m_resizingWorkspace = false;
		this->WorkspaceGutter().ReleasePointerCaptures();
	}

	void ShellPage::BeginFilesNewItem(bool folder)
	{
		m_creatingFolder = folder;
		this->FilesNewItemKindText().Text(folder ? L"Folder" : L"File");
		this->FilesNewItemBox().Text(folder ? L"new-folder" : L"untitled.md");
		this->FilesNewItemErrorText().Visibility(Visibility::Collapsed);
		this->FilesNewItemErrorText().Text(L"");
		this->FilesNewItemPathText().Text(winrt::hstring{ FilesCreateTargetBreadcrumb() });

		this->FilesNewItemPanel().Visibility(Visibility::Visible);
		this->FilesNewItemBox().Focus(FocusState::Programmatic);
		this->FilesNewItemBox().SelectAll();
	}

	void ShellPage::CreateFilesNewItem()
	{
		auto workdir = m_core->CurrentWorkdir();
		if (workdir.empty())
		{
			this->FilesNewItemErrorText().Text(L"尚未打开工作区。");
			this->FilesNewItemErrorText().Visibility(Visibility::Visible);
			return;
		}

		auto nameText = this->FilesNewItemBox().Text();
		auto name = std::wstring{ nameText.c_str(), nameText.size() };
		if (name.empty() || name.find_first_of(L"\\/") != std::wstring::npos)
		{
			this->FilesNewItemErrorText().Text(L"请输入不包含路径分隔符的名称。");
			this->FilesNewItemErrorText().Visibility(Visibility::Visible);
			return;
		}

		std::error_code ec;
		auto root = std::filesystem::path{ ToWide(workdir) };
		auto targetDir = root;
		if (!m_selectedFilesFolder.empty())
		{
			auto selectedDir = root / m_selectedFilesFolder;
			if (std::filesystem::exists(selectedDir, ec) && std::filesystem::is_directory(selectedDir, ec))
			{
				targetDir = std::move(selectedDir);
			}
		}
		auto target = targetDir / name;
		if (std::filesystem::exists(target, ec))
		{
			this->FilesNewItemErrorText().Text(L"同名项目已存在。");
			this->FilesNewItemErrorText().Visibility(Visibility::Visible);
			return;
		}

		if (m_creatingFolder)
		{
			std::filesystem::create_directory(target, ec);
		}
		else
		{
			std::ofstream file{ target };
			if (!file)
			{
				ec = std::make_error_code(std::errc::io_error);
			}
		}

		if (ec)
		{
			this->FilesNewItemErrorText().Text(L"创建失败。");
			this->FilesNewItemErrorText().Visibility(Visibility::Visible);
			return;
		}

		CancelFilesNewItem();
		RefreshFiles();
		SetWorkspaceTab(2);
	}

	void ShellPage::CancelFilesNewItem()
	{
		this->FilesNewItemPanel().Visibility(Visibility::Collapsed);
		this->FilesNewItemErrorText().Visibility(Visibility::Collapsed);
		this->FilesNewItemErrorText().Text(L"");
	}

	void ShellPage::ToggleFileDirectory(std::wstring relative)
	{
		if (relative.empty())
		{
			return;
		}

		if (m_expandedFileDirectories.contains(relative))
		{
			m_expandedFileDirectories.erase(relative);
		}
		else
		{
			m_expandedFileDirectories.insert(relative);
		}

		m_selectedFilesFolder = (m_selectedFilesFolder == relative) ? std::wstring{} : std::move(relative);
		m_selectedFilesPath.clear();
		if (this->FilesNewItemPanel().Visibility() == Visibility::Visible)
		{
			this->FilesNewItemPathText().Text(winrt::hstring{ FilesCreateTargetBreadcrumb() });
		}
		RefreshFiles();
	}

	bool ShellPage::IsFileDirectoryExpanded(std::wstring const& relative) const
	{
		return m_expandedFileDirectories.contains(relative);
	}

	std::wstring ShellPage::FilesCreateTargetBreadcrumb() const
	{
		if (m_selectedFilesFolder.empty())
		{
			return L"./";
		}
		return L"./" + m_selectedFilesFolder + L"/";
	}

	std::wstring ShellPage::FindSessionTitle(std::wstring const& sessionId)
	{
		if (sessionId.empty())
		{
			return {};
		}

		for (auto const& session : m_core->ListSessions())
		{
			if (ToWide(session.sessionId) == sessionId)
			{
				return ToWide(session.title.empty() ? session.sessionId : session.title);
			}
		}
		return sessionId;
	}

	std::wstring ShellPage::ToWide(std::string_view text)
	{
		auto value = winrt::to_hstring(std::string{ text });
		return std::wstring{ value.c_str(), value.size() };
	}

	void ShellPage::RefreshUsage()
	{
		// Surfaced via ChatPage::SetUsage in the turn_ended handler; nothing to do here.
	}

	void ShellPage::RefreshEnvironment()
	{
		// Workdir label in the sidebar and the chat header workspace pill.
		auto workdir = m_core->CurrentWorkdir();
		if (!workdir.empty())
		{
			this->SidebarControl().SetWorkdir(winrt::hstring{ ToWide(workdir) });
			// The workspace pill shows the folder name (basename), not the full path.
			std::wstring wworkdir = ToWide(workdir);
			auto lastSep = wworkdir.find_last_of(L"\\/");
			auto folderName = (lastSep != std::wstring::npos && lastSep + 1 < wworkdir.size())
				? wworkdir.substr(lastSep + 1)
				: wworkdir;
			this->Chat().SetWorkspaceName(winrt::hstring{ folderName });
			this->TopProjectText().Text(winrt::hstring{ folderName });
		}

		// Real git branch (empty if not a repo).
		auto branch = m_core->CurrentBranch();
		m_currentBranch = branch.empty() ? L"main" : std::wstring{ ToWide(branch) };
		this->Chat().SetBranchName(winrt::hstring{ m_currentBranch });
		RefreshBranchInfo();
		RefreshFiles();
	}

	void ShellPage::RefreshGitChanges()
	{
		auto list = this->GitFileList();
		auto children = list.Children();
		children.Clear();
		this->TopDirtyText().Text(winrt::hstring{ std::to_wstring(m_gitChanges.size()) });
		this->TopDirtyBadge().Visibility(m_gitChanges.empty() ? Visibility::Collapsed : Visibility::Visible);

		if (m_gitChanges.empty())
		{
			TextBlock empty;
			empty.Text(L"\u6682\u65E0\u6587\u4EF6\u53D8\u66F4\u3002\u5F00\u59CB\u5BF9\u8BDD\u540E\uFF0CGit \u547D\u4EE4\u7684\u7ED3\u679C\u5C06\u5728\u6B64\u663E\u793A\u3002");
			empty.Style(Application::Current().Resources().Lookup(winrt::box_value(L"ContextItemMetaTextStyle"))
			               .try_as<winrt::Microsoft::UI::Xaml::Style>());
			empty.TextWrapping(TextWrapping::Wrap);
			children.Append(empty);
			return;
		}

		for (auto const& entry : m_gitChanges)
		{
			Grid row;
			ColumnDefinition statusCol;
			statusCol.Width(GridLength{20, GridUnitType::Pixel});
			ColumnDefinition pathCol;
			pathCol.Width(GridLength{1, GridUnitType::Star});
			ColumnDefinition deltaCol;
			deltaCol.Width(GridLength{0, GridUnitType::Auto});
			row.ColumnDefinitions().Append(statusCol);
			row.ColumnDefinitions().Append(pathCol);
			row.ColumnDefinitions().Append(deltaCol);
			row.ColumnSpacing(6);
			row.Padding(Thickness{0, 2, 0, 2});

			// Status indicator glyph.
			TextBlock statusIcon;
			statusIcon.FontFamily(Media::FontFamily(L"Consolas"));
			statusIcon.FontSize(11);
			statusIcon.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
			statusIcon.VerticalAlignment(VerticalAlignment::Center);
			wchar_t statusChar = entry.status;
			statusIcon.Text(winrt::hstring{ &statusChar, 1 });
			statusIcon.Foreground(Media::SolidColorBrush(
				statusChar == L'A' ? Windows::UI::Color{255, 22, 163, 74}
				: statusChar == L'D' ? Windows::UI::Color{255, 191, 22, 22}
				: Windows::UI::Color{255, 234, 179, 8})); // modified = yellow
			Grid::SetColumn(statusIcon, 0);
			row.Children().Append(statusIcon);

			// File path.
			TextBlock pathBlock;
			pathBlock.Text(winrt::hstring{ entry.path });
			pathBlock.FontSize(12);
			pathBlock.TextTrimming(TextTrimming::CharacterEllipsis);
			pathBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
			pathBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(pathBlock, 1);
			row.Children().Append(pathBlock);

			// +/- delta counts.
			TextBlock deltaBlock;
			std::wstringstream deltaStream;
			if (entry.additions > 0) deltaStream << L"+" << entry.additions << L" ";
			if (entry.deletions > 0) deltaStream << L"-" << entry.deletions;
			deltaBlock.Text(winrt::hstring{ deltaStream.str() });
			deltaBlock.FontSize(11);
			deltaBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
			deltaBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(deltaBlock, 2);
			row.Children().Append(deltaBlock);

			children.Append(row);
		}
	}

	void ShellPage::RefreshBranchInfo()
	{
		this->GitBranchText().Text(winrt::hstring{ L"\u5206\u652F: " + m_currentBranch });
		this->TopBranchText().Text(winrt::hstring{ m_currentBranch });
	}

	void ShellPage::RefreshProgress()
	{
		auto list = this->ProgressList();
		auto children = list.Children();
		children.Clear();

		auto noProgress = m_completedSteps.empty();
		this->ProgressEmptyText().Visibility(noProgress ? Visibility::Visible : Visibility::Collapsed);

		if (noProgress)
		{
			return;
		}

		for (auto const& step : m_completedSteps)
		{
			Grid row;
			ColumnDefinition checkCol;
			checkCol.Width(GridLength{18, GridUnitType::Pixel});
			ColumnDefinition labelCol;
			labelCol.Width(GridLength{1, GridUnitType::Star});
			row.ColumnDefinitions().Append(checkCol);
			row.ColumnDefinitions().Append(labelCol);
			row.ColumnSpacing(6);
			row.Padding(Thickness{0, 2, 0, 2});

			// Green check glyph.
			TextBlock check;
			check.Text(L"\uE73E");
			check.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			check.FontSize(11);
			check.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 22, 163, 74}));
			check.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(check, 0);
			row.Children().Append(check);

			// Step label.
			TextBlock label;
			label.Text(winrt::hstring{ step });
			label.FontSize(12);
			label.TextTrimming(TextTrimming::CharacterEllipsis);
			label.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
			label.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(label, 1);
			row.Children().Append(label);

			children.Append(row);
		}
	}

	void ShellPage::RefreshFiles()
	{
		auto children = this->FilesList().Children();
		children.Clear();

		auto workdir = m_core->CurrentWorkdir();
		if (workdir.empty())
		{
			this->FilesRootText().Text(L".");
			this->FilesEmptyText().Text(L"\u5C1A\u672A\u6253\u5F00\u5DE5\u4F5C\u533A\u3002");
			this->FilesEmptyText().Visibility(Visibility::Visible);
			return;
		}

		auto rootWide = ToWide(workdir);
		auto lastSep = rootWide.find_last_of(L"\\/");
		auto folderName = (lastSep != std::wstring::npos && lastSep + 1 < rootWide.size())
			? rootWide.substr(lastSep + 1)
			: rootWide;
		this->FilesRootText().Text(winrt::hstring{ L"./" + folderName });

		struct Entry
		{
			std::wstring name;
			std::wstring meta;
			std::wstring relative;
			std::wstring parentRelative;
			std::wstring extension;
			bool directory = false;
			std::uintmax_t size = 0;
			std::size_t depth = 0;
		};

		std::vector<Entry> entries;
		std::error_code ec;
		auto root = std::filesystem::path{ rootWide };
		auto options = std::filesystem::directory_options::skip_permission_denied;
		constexpr auto kMaxEntries = std::size_t{160};
		constexpr auto kMaxVisited = std::size_t{3000};

		auto query = std::wstring{ this->FilesSearchBox().Text().c_str(), this->FilesSearchBox().Text().size() };
		auto toLower = [](std::wstring value) {
			std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return std::towlower(ch); });
			return value;
		};
		query = toLower(std::move(query));
		auto searching = !query.empty();

		if (!m_selectedFilesFolder.empty())
		{
			auto selectedDir = root / m_selectedFilesFolder;
			if (!std::filesystem::exists(selectedDir, ec) || !std::filesystem::is_directory(selectedDir, ec))
			{
				m_selectedFilesFolder.clear();
			}
			ec.clear();
		}

		auto shouldSkipDirectory = [&](std::filesystem::path const& path) {
			auto folder = toLower(path.filename().wstring());
			return folder == L".git"
				|| folder == L".vs"
				|| folder == L"build"
				|| folder == L"node_modules"
				|| folder == L"out"
				|| folder == L"packages";
		};

		auto appendEntry = [&](std::filesystem::directory_entry const& dirEntry) {
			auto name = dirEntry.path().filename().wstring();
			if (name.empty()) return;

			std::error_code relEc;
			auto rel = std::filesystem::relative(dirEntry.path(), root, relEc).generic_wstring();
			if (relEc)
			{
				rel = name;
			}
			auto parentRelative = std::wstring{};
			auto slash = rel.find_last_of(L'/');
			if (slash != std::wstring::npos)
			{
				parentRelative = rel.substr(0, slash);
			}
			auto haystack = toLower(rel);
			if (searching && haystack.find(query) == std::wstring::npos)
			{
				return;
			}

			std::error_code typeEc;
			auto isDir = dirEntry.is_directory(typeEc);
			auto isRegular = !isDir && dirEntry.is_regular_file(typeEc);
			auto size = std::uintmax_t{0};
			auto extension = toLower(dirEntry.path().extension().wstring());
			if (isRegular)
			{
				size = dirEntry.file_size(typeEc);
				if (typeEc) size = 0;
			}
			auto meta = searching ? rel : std::wstring{};
			auto depth = static_cast<std::size_t>(std::count(rel.begin(), rel.end(), L'/'));
			entries.push_back(Entry{
				std::move(name),
				std::move(meta),
				std::move(rel),
				std::move(parentRelative),
				std::move(extension),
				isDir,
				size,
				depth
			});
		};

		{
			auto maxDepth = searching ? 4 : 20;
			auto it = std::filesystem::recursive_directory_iterator(root, options, ec);
			auto end = std::filesystem::recursive_directory_iterator();
			auto visited = std::size_t{0};
			while (!ec && it != end && entries.size() < kMaxEntries && visited < kMaxVisited)
			{
				++visited;
				std::error_code typeEc;
				auto isDir = it->is_directory(typeEc);
				if (isDir)
				{
					if (shouldSkipDirectory(it->path()))
					{
						it.disable_recursion_pending();
						it.increment(ec);
						continue;
					}
					std::error_code relEc;
					auto rel = std::filesystem::relative(it->path(), root, relEc).generic_wstring();
					if (!searching && (relEc || !IsFileDirectoryExpanded(rel)))
					{
						it.disable_recursion_pending();
					}
					else if (it.depth() >= maxDepth)
					{
						it.disable_recursion_pending();
					}
				}
				appendEntry(*it);
				it.increment(ec);
			}
		}

		if (ec && entries.empty())
		{
			this->FilesEmptyText().Text(L"\u65E0\u6CD5\u8BFB\u53D6\u5DE5\u4F5C\u533A\u6587\u4EF6\u3002");
			this->FilesEmptyText().Visibility(Visibility::Visible);
			return;
		}

		std::sort(entries.begin(), entries.end(), [&](Entry const& a, Entry const& b) {
			if (a.parentRelative == b.parentRelative && a.directory != b.directory)
			{
				return a.directory;
			}
			auto left = a.relative.empty() ? a.name : a.relative;
			auto right = b.relative.empty() ? b.name : b.relative;
			left = toLower(std::move(left));
			right = toLower(std::move(right));
			return left < right;
		});

		this->FilesEmptyText().Visibility(entries.empty() ? Visibility::Visible : Visibility::Collapsed);
		this->FilesEmptyText().Text(query.empty()
			? L"\u6682\u65E0\u53EF\u663E\u793A\u6587\u4EF6\u3002"
			: L"\u6CA1\u6709\u5339\u914D\u7684\u6587\u4EF6\u3002");

		for (auto const& entry : entries)
		{
			auto expanded = entry.directory && (searching || IsFileDirectoryExpanded(entry.relative));
			auto selected = entry.directory
				? entry.relative == m_selectedFilesFolder
				: entry.relative == m_selectedFilesPath;

			Border row;
			winrt::Microsoft::UI::Xaml::CornerRadius rowRadius{10, 10, 10, 10};
			row.CornerRadius(rowRadius);
			row.Padding(Thickness{4 + static_cast<double>(std::min<std::size_t>(entry.depth, 4)) * 14.0, 4, 6, 4});
			row.MinHeight(28);
			row.Background(Media::SolidColorBrush(selected
				? Windows::UI::Color{255, 236, 236, 232}
				: Windows::UI::Color{0, 255, 255, 255}));

			Grid grid;
			ColumnDefinition caretCol;
			caretCol.Width(GridLength{14, GridUnitType::Pixel});
			grid.ColumnDefinitions().Append(caretCol);
			ColumnDefinition iconCol;
			iconCol.Width(GridLength{18, GridUnitType::Pixel});
			ColumnDefinition nameCol;
			nameCol.Width(GridLength{1, GridUnitType::Star});
			ColumnDefinition metaCol;
			metaCol.Width(GridLength{0, GridUnitType::Auto});
			ColumnDefinition actionCol;
			actionCol.Width(GridLength{22, GridUnitType::Pixel});
			grid.ColumnDefinitions().Append(iconCol);
			grid.ColumnDefinitions().Append(nameCol);
			grid.ColumnDefinitions().Append(metaCol);
			grid.ColumnDefinitions().Append(actionCol);
			grid.ColumnSpacing(4);

			TextBlock caret;
			caret.Text(entry.directory ? (expanded ? L"\uE70E" : L"\uE70D") : L"");
			caret.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			caret.FontSize(9);
			caret.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 180, 180, 172}));
			caret.VerticalAlignment(VerticalAlignment::Center);
			caret.HorizontalAlignment(HorizontalAlignment::Center);
			Grid::SetColumn(caret, 0);
			grid.Children().Append(caret);

			TextBlock icon;
			auto iconGlyph = std::wstring{ L"\uE8A5" };
			if (entry.directory)
			{
				iconGlyph = expanded ? L"\uE838" : L"\uE8B7";
			}
			else if (entry.extension == L".cpp"
				|| entry.extension == L".c"
				|| entry.extension == L".cc"
				|| entry.extension == L".h"
				|| entry.extension == L".hpp"
				|| entry.extension == L".cs"
				|| entry.extension == L".js"
				|| entry.extension == L".jsx"
				|| entry.extension == L".ts"
				|| entry.extension == L".tsx"
				|| entry.extension == L".py"
				|| entry.extension == L".rs"
				|| entry.extension == L".go"
				|| entry.extension == L".json"
				|| entry.extension == L".toml"
				|| entry.extension == L".yaml"
				|| entry.extension == L".yml")
			{
				iconGlyph = L"\uE943";
			}
			icon.Text(winrt::hstring{ iconGlyph });
			icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			icon.FontSize(12);
			icon.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 133, 133, 125}));
			icon.VerticalAlignment(VerticalAlignment::Center);
			icon.HorizontalAlignment(HorizontalAlignment::Center);
			Grid::SetColumn(icon, 1);
			grid.Children().Append(icon);

			TextBlock name;
			name.Text(winrt::hstring{ entry.name });
			name.FontSize(12);
			name.TextTrimming(TextTrimming::CharacterEllipsis);
			name.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 36, 36, 36}));
			name.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(name, 2);
			grid.Children().Append(name);

			TextBlock meta;
			if (!entry.meta.empty())
			{
				meta.Text(winrt::hstring{ entry.meta });
			}
			else if (entry.directory)
			{
				meta.Text(L"");
			}
			else if (entry.size >= 1024 * 1024)
			{
				wchar_t buf[32];
				std::swprintf(buf, std::size(buf), L"%.1f MB", static_cast<double>(entry.size) / (1024.0 * 1024.0));
				meta.Text(buf);
			}
			else if (entry.size >= 1024)
			{
				wchar_t buf[32];
				std::swprintf(buf, std::size(buf), L"%.1f KB", static_cast<double>(entry.size) / 1024.0);
				meta.Text(buf);
			}
			else
			{
				meta.Text(winrt::hstring{ std::to_wstring(entry.size) + L" B" });
			}
			meta.FontSize(11);
			meta.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 180, 180, 172}));
			meta.VerticalAlignment(VerticalAlignment::Center);
			meta.Margin(Thickness{8, 0, 0, 0});
			Grid::SetColumn(meta, 3);
			grid.Children().Append(meta);

			TextBlock action;
			action.Text(L"\uE710");
			action.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			action.FontSize(10);
			action.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 133, 133, 125}));
			action.HorizontalAlignment(HorizontalAlignment::Center);
			action.VerticalAlignment(VerticalAlignment::Center);
			action.Opacity(0);
			action.Tapped([this, relative = entry.relative, isDirectory = entry.directory](winrt::Windows::Foundation::IInspectable const&,
			                                                                             winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
			{
				args.Handled(true);
				this->Chat().AddComposerAttachment(winrt::hstring{ relative }, isDirectory);
				this->Chat().FocusComposer();
			});
			Grid::SetColumn(action, 4);
			grid.Children().Append(action);

			row.Tapped([this, relative = entry.relative, isDirectory = entry.directory](winrt::Windows::Foundation::IInspectable const&,
			                                                                          winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
			{
				args.Handled(true);
				if (isDirectory)
				{
					ToggleFileDirectory(relative);
					return;
				}

				m_selectedFilesPath = relative;
				m_selectedFilesFolder.clear();
				if (this->FilesNewItemPanel().Visibility() == Visibility::Visible)
				{
					this->FilesNewItemPathText().Text(winrt::hstring{ FilesCreateTargetBreadcrumb() });
				}
				RefreshFiles();
			});

			row.PointerEntered([row, action](winrt::Windows::Foundation::IInspectable const&,
			                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				row.Background(Media::SolidColorBrush(Windows::UI::Color{255, 245, 245, 244}));
				action.Opacity(0.8);
			});
			row.PointerExited([row, action, selected](winrt::Windows::Foundation::IInspectable const&,
			                                          winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&)
			{
				row.Background(Media::SolidColorBrush(selected
					? Windows::UI::Color{255, 236, 236, 232}
					: Windows::UI::Color{0, 255, 255, 255}));
				action.Opacity(0);
			});

			row.Child(grid);
			children.Append(row);
		}
	}

	void ShellPage::CollectGitChange(nlohmann::json const& args)
	{
		// Try to extract a file path from common argument keys used by git tools.
		static const std::vector<std::string> kPathKeys = {"file_path", "path", "filename"};
		std::wstring wide;
		for (auto const& key : kPathKeys)
		{
			if (args.contains(key) && args[key].is_string())
			{
				wide = ToWide(args[key].get<std::string>());
				if (!wide.empty()) break;
			}
		}
		if (wide.empty()) return;

		// Extract the subcommand to determine status letter.
		wchar_t statusLetter = L'M'; // default: modified
		if (args.contains("subcommand") && args["subcommand"].is_string())
		{
			auto sub = args["subcommand"].get<std::string>();
			if (sub == "add") statusLetter = L'A';
			else if (sub == "rm" || sub == "delete") statusLetter = L'D';
		}

		// Upsert: if the same path is already tracked, update it; otherwise prepend.
		auto it = std::find_if(m_gitChanges.begin(), m_gitChanges.end(),
		                       [&wide](GitChangeEntry const& e) { return e.path == wide; });
		if (it != m_gitChanges.end())
		{
			it->status = statusLetter;
		}
		else
		{
			m_gitChanges.insert(m_gitChanges.begin(), GitChangeEntry{std::move(wide), statusLetter, 0, 0});
			if (m_gitChanges.size() > 50)
			{
				m_gitChanges.resize(50);
			}
		}
		RefreshGitChanges();
	}

	std::wstring ShellPage::FormatTokenCount(std::int64_t tokens)
	{
		if (tokens < 1000)
		{
			return std::to_wstring(tokens);
		}
		if (tokens < 100000)
		{
			wchar_t buf[16];
			std::swprintf(buf, std::size(buf), L"%.1fk", static_cast<double>(tokens) / 1000.0);
			return buf;
		}
		wchar_t buf[16];
		std::swprintf(buf, std::size(buf), L"%.1fM", static_cast<double>(tokens) / 1000000.0);
		return buf;
	}

} // namespace winrt::CodeHarness::Desktop::Views::implementation

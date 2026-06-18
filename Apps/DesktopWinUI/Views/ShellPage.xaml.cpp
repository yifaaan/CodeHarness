#include "Views/ShellPage.xaml.h"
#include "Controls/Sidebar.xaml.h"
#include "Views/ChatPage.xaml.h"

#include <algorithm>
#include <cwchar>
#include <future>
#include <sstream>
#include <utility>

#include <winrt/base.h>
#include <winrt/CodeHarness.Desktop.Controls.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
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
		LoadSessions();
	}

	void ShellPage::FocusComposer()
	{
		this->Chat().FocusComposer();
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
					if (loop.contains("AssistantDelta"))
					{
						chat.AppendAssistantDelta(winrt::hstring{ ToWide(loop["AssistantDelta"].value("text", "")) });
					}
					else if (loop.contains("StepStarted"))
					{
						m_currentSteps = loop["StepStarted"].value("step", m_currentSteps);
						RefreshUsage();
					}
					else if (loop.contains("ToolCallStarted"))
					{
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
					chat.SetRunning(true);
					RefreshUsage();
				}
				else if (event.type == "turn_ended")
				{
					m_running = false;
					chat.SetRunning(false);
					auto result = event.payload.value("result", nlohmann::json::object());
					auto usage = result.value("usage", nlohmann::json::object());
					std::int64_t turnTokens = 0;
					turnTokens += usage.value("input_other", static_cast<std::int64_t>(0));
					turnTokens += usage.value("output", static_cast<std::int64_t>(0));
					turnTokens += usage.value("input_cache_read", static_cast<std::int64_t>(0));
					turnTokens += usage.value("input_cache_creation", static_cast<std::int64_t>(0));
					m_totalTokens += turnTokens;
					m_currentSteps = 0;
					RefreshUsage();
				}
				else if (event.type == "error")
				{
					chat.AppendStatusMessage(winrt::hstring{ L"\u9519\u8BEF: " + ToWide(event.payload.value("message", "")) });
					m_running = false;
					chat.SetRunning(false);
				}
			});
		});

		m_core->SetApprovalCallback([this, queue](const desktop::DesktopPermissionRequest& request) {
			auto promise = std::make_shared<std::promise<bool>>();
			auto future = promise->get_future();
			queue.TryEnqueue([this, request, promise]() {
				ContentDialog dialog;
				dialog.XamlRoot(this->XamlRoot());
				dialog.Title(box_value(L"\u5141\u8BB8\u6267\u884C\u5DE5\u5177\uFF1F"));
				dialog.Content(box_value(winrt::hstring{ ToWide(request.description) }));
				dialog.PrimaryButtonText(L"\u5141\u8BB8");
				dialog.CloseButtonText(L"\u62D2\u7EDD");
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
	}

	void ShellPage::LoadSessions()
	{
		auto sessions = m_core->ListSessions();
		winrt::Windows::Foundation::Collections::IVector<winrt::hstring> titles =
			winrt::single_threaded_vector<winrt::hstring>();
		for (const auto& session : sessions)
		{
			auto title = session.title.empty() ? session.sessionId : session.title;
			titles.Append(winrt::hstring{ ToWide(title) });
		}
		this->SidebarControl().SetSessions(titles.GetView());
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
		auto sessionId = m_core->CreateSession("desktop");
		if (sessionId.empty())
		{
			this->Chat().SetStatus(winrt::hstring{ L"\u65E0\u6CD5\u521B\u5EFA\u4F1A\u8BDD\uFF0C\u8BF7\u68C0\u67E5 config.toml" });
			return;
		}
		this->Chat().ResetTranscript();
		this->Chat().SetStatus(winrt::hstring{ L"\u65B0\u5EFA\u4F1A\u8BDD\u5DF2\u521B\u5EFA" });
		// Reset accumulated state for the fresh session.
		m_totalTokens = 0;
		m_currentSteps = 0;
		m_currentBranch = L"main";
		m_gitChanges.clear();
		RefreshUsage();
		RefreshGitChanges();
		RefreshBranchInfo();
		LoadSessions();
	}

	void ShellPage::ResumeSession(std::wstring titleHint)
	{
		auto sessions = m_core->ListSessions();
		std::string targetId;
		for (const auto& session : sessions)
		{
			auto title = session.title.empty() ? session.sessionId : session.title;
			if (ToWide(title) == titleHint)
			{
				targetId = session.sessionId;
				break;
			}
		}
		if (targetId.empty() && !sessions.empty())
		{
			targetId = sessions.front().sessionId;
		}
		if (targetId.empty())
		{
			return;
		}
		auto resumed = m_core->ResumeSession(targetId);
		this->Chat().SetStatus(winrt::hstring{ resumed.empty() ? L"\u6062\u590D\u5931\u8D25" : L"\u4F1A\u8BDD\u5DF2\u6062\u590D" });
	}

	void ShellPage::CancelPrompt()
	{
		m_core->Cancel();
		this->Chat().SetStatus(winrt::hstring{ L"\u6B63\u5728\u53D6\u6D88\u2026" });
	}

	void ShellPage::OpenSettings()
	{
		StackPanel panel;
		panel.Spacing(12);
		panel.Padding(Thickness{0, 8, 0, 0});

		TextBlock model;
		model.Text(L"\u6A21\u578B: ChatGLM");
		model.FontSize(14);
		model.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(model);

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

	std::wstring ShellPage::ToWide(std::string_view text)
	{
		auto value = winrt::to_hstring(std::string{ text });
		return std::wstring{ value.c_str(), value.size() };
	}

	void ShellPage::RefreshUsage()
	{
		// Usage display removed from the right panel (Git tools only).
		// Token counting is kept internally for future use.
	}

	void ShellPage::RefreshGitChanges()
	{
		auto list = this->GitFileList();
		auto children = list.Children();
		children.Clear();

		if (m_gitChanges.empty())
		{
			TextBlock empty;
			empty.Text(L"\u6682\u65E0\u6587\u4EF6\u53D8\u66F4\u3002\u5F00\u59CB\u5BF9\u8BDD\u540E\uFF0CGit \u547D\u4EE4\u7684\u7ED3\u679C\u5C06\u5728\u6B64\u663E\u793A\u3002");
			empty.Style(this->Resources().Lookup(winrt::box_value(L"ContextItemMetaTextStyle"))
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
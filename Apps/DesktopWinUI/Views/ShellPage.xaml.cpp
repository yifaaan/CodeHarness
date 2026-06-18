#include "Views/ShellPage.xaml.h"
#include "Controls/Sidebar.xaml.h"
#include "Views/ChatPage.xaml.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cwctype>
#include <future>
#include <utility>

#include <winrt/base.h>
#include <winrt/CodeHarness.Desktop.Controls.h>
#include <winrt/Microsoft.UI.Dispatching.h>
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
						// Live step counter for the active turn.
						m_currentSteps = loop["StepStarted"].value("step", m_currentSteps);
						RefreshUsage();
					}
					else if (loop.contains("ToolCallStarted"))
					{
						auto tool = loop["ToolCallStarted"];
						auto name = ToWide(tool.value("name", ""));
						chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ L"running\u2026" }, false);
						// Track activity + open files for the Context panel.
						if (tool.contains("args"))
						{
							CollectFilePath(tool["args"]);
						}
						m_activity.insert(m_activity.begin(), ToolActivityEntry{ name, L"running", false });
						if (m_activity.size() > 20)
						{
							m_activity.resize(20);
						}
						RefreshActivity();
						RefreshOpenFiles();
					}
					else if (loop.contains("ToolResult"))
					{
						auto tool = loop["ToolResult"];
						auto status = tool.value("status", "");
						auto detail = status == "error" ? L"failed" : L"completed";
						auto name = ToWide(tool.value("name", ""));
						bool isError = (status == "error");
						chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ detail }, isError);
						// Flip the most recent matching activity entry's status.
						for (auto& entry : m_activity)
						{
							if (entry.name == name && entry.status == L"running")
							{
								entry.status = detail;
								entry.isError = isError;
								break;
							}
						}
						RefreshActivity();
					}
					else if (loop.contains("PermissionDenied"))
					{
						auto permission = loop["PermissionDenied"];
						auto name = ToWide(permission.value("name", ""));
						chat.AppendToolCard(winrt::hstring{ name }, winrt::hstring{ L"permission denied" }, true);
						for (auto& entry : m_activity)
						{
							if (entry.name == name && entry.status == L"running")
							{
								entry.status = L"denied";
								entry.isError = true;
								break;
							}
						}
						RefreshActivity();
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
					// Accumulate token usage for this turn into the running total.
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
					chat.AppendStatusMessage(winrt::hstring{ L"Error: " + ToWide(event.payload.value("message", "")) });
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
				dialog.Title(box_value(L"Allow tool execution?"));
				dialog.Content(box_value(winrt::hstring{ ToWide(request.description) }));
				dialog.PrimaryButtonText(L"Allow");
				dialog.CloseButtonText(L"Deny");
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
				dialog.Title(box_value(L"CodeHarness question"));
				dialog.Content(box_value(winrt::hstring{ ToWide(request.question) }));
				dialog.PrimaryButtonText(request.options.empty() ? L"OK" : winrt::hstring{ ToWide(request.options.front()) });
				dialog.CloseButtonText(L"Cancel");
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
		// Convert UTF-16 prompt to UTF-8 for the core.
		winrt::hstring wide{ text.c_str(), static_cast<uint32_t>(text.size()) };
		std::string utf8 = winrt::to_string(wide);
		m_core->Prompt(std::move(utf8), [this, chat](std::string message) {
			auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
			if (!queue)
			{
				return;
			}
			queue.TryEnqueue([this, chat, message = std::move(message)]() {
				chat.AppendStatusMessage(winrt::hstring{ L"Error: " + ToWide(message) });
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
			this->Chat().SetStatus(winrt::hstring{ L"Could not create session. Check config.toml." });
			return;
		}
		this->Chat().ResetTranscript();
		this->Chat().SetStatus(winrt::hstring{ L"New session created" });
		// Reset the Context panel accumulated state for the fresh session.
		m_totalTokens = 0;
		m_currentSteps = 0;
		m_activity.clear();
		m_openFiles.clear();
		RefreshUsage();
		RefreshActivity();
		RefreshOpenFiles();
		LoadSessions();
	}

	void ShellPage::ResumeSession(std::wstring titleHint)
	{
		// The Sidebar reports the selected session title; resolve it to a
		// session id in the live list and resume that session.
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
			targetId = sessions.front().sessionId; // best-effort fallback
		}
		if (targetId.empty())
		{
			return;
		}
		auto resumed = m_core->ResumeSession(targetId);
		this->Chat().SetStatus(winrt::hstring{ resumed.empty() ? L"Resume failed" : L"Session resumed" });
	}

	void ShellPage::CancelPrompt()
	{
		m_core->Cancel();
		this->Chat().SetStatus(winrt::hstring{ L"Cancelling" });
	}

	void ShellPage::OpenSettings()
	{
		StackPanel panel;
		panel.Spacing(12);
		panel.Padding(Thickness{0, 8, 0, 0});

		TextBlock model;
		model.Text(L"Model: ChatGLM");
		model.FontSize(14);
		model.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(model);

		TextBlock perm;
		perm.Text(L"Permission mode: Manual (approve each tool)");
		perm.FontSize(14);
		perm.TextWrapping(TextWrapping::Wrap);
		perm.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
		panel.Children().Append(perm);

		TextBlock hint;
		hint.Text(L"Edit ~/.codeharness/config.toml to change models, providers, hooks, and skills.");
		hint.FontSize(12);
		hint.TextWrapping(TextWrapping::Wrap);
		hint.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
		panel.Children().Append(hint);

		ContentDialog dialog;
		dialog.XamlRoot(this->XamlRoot());
		dialog.Title(box_value(L"Settings"));
		dialog.Content(panel);
		dialog.CloseButtonText(L"Done");
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
		// The 128k denominator is a hardcoded stand-in; the core does not yet
		// surface per-model maxContextTokens to the desktop layer.
		constexpr std::int64_t kContextWindow = 128 * 1024;
		auto contextText = FormatTokenCount(m_totalTokens) + L" / 128k";
		this->ContextValueText().Text(winrt::hstring{ contextText });
		// Clamp the progress bar so it never exceeds the visible band.
		double ratio = kContextWindow > 0 ? static_cast<double>(m_totalTokens) / static_cast<double>(kContextWindow) : 0.0;
		if (ratio < 0.0) ratio = 0.0;
		if (ratio > 1.0) ratio = 1.0;
		this->ContextProgressBar().Value(ratio * 100.0);
		this->StepsValueText().Text(winrt::hstring{ std::to_wstring(m_currentSteps) });
	}

	void ShellPage::RefreshActivity()
	{
		auto list = this->ActivityList();
		auto children = list.Children();
		children.Clear();
		if (m_activity.empty())
		{
			TextBlock empty;
			empty.Text(L"No recent activity. Start a chat to see tool calls and updates here.");
			empty.Style(this->Resources().Lookup(winrt::box_value(L"ContextItemMetaTextStyle"))
			               .try_as<winrt::Microsoft::UI::Xaml::Style>());
			empty.TextWrapping(TextWrapping::Wrap);
			children.Append(empty);
			return;
		}
		for (auto const& entry : m_activity)
		{
			Grid row;
			ColumnDefinition iconCol;
			iconCol.Width(GridLength{16, GridUnitType::Pixel});
			ColumnDefinition nameCol;
			nameCol.Width(GridLength{1, GridUnitType::Star});
			ColumnDefinition statusCol;
			statusCol.Width(GridLength{0, GridUnitType::Auto});
			row.ColumnDefinitions().Append(iconCol);
			row.ColumnDefinitions().Append(nameCol);
			row.ColumnDefinitions().Append(statusCol);
			row.ColumnSpacing(6);

			TextBlock icon;
			icon.Text(entry.isError ? L"\uE783" : (entry.status == L"running" ? L"\uEB1F" : L"\uE73E"));
			icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			icon.FontSize(11);
			icon.Foreground(Media::SolidColorBrush(entry.isError ? Windows::UI::Color{255, 191, 22, 22}
			                                                    : (entry.status == L"running" ? Windows::UI::Color{255, 138, 138, 134}
			                                                                                  : Windows::UI::Color{255, 22, 163, 74})));
			icon.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(icon, 0);
			row.Children().Append(icon);

			TextBlock nameBlock;
			nameBlock.Text(winrt::hstring{ entry.name });
			nameBlock.FontSize(12);
			nameBlock.TextTrimming(TextTrimming::CharacterEllipsis);
			nameBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
			nameBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(nameBlock, 1);
			row.Children().Append(nameBlock);

			TextBlock statusBlock;
			statusBlock.Text(winrt::hstring{ entry.status });
			statusBlock.FontSize(11);
			statusBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
			statusBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(statusBlock, 2);
			row.Children().Append(statusBlock);

			children.Append(row);
		}
	}

	void ShellPage::RefreshOpenFiles()
	{
		auto list = this->OpenFilesList();
		auto children = list.Children();
		children.Clear();
		if (m_openFiles.empty())
		{
			TextBlock empty;
			empty.Text(L"No files opened yet. Tools that read or write files will appear here.");
			empty.Style(this->Resources().Lookup(winrt::box_value(L"ContextItemMetaTextStyle"))
			               .try_as<winrt::Microsoft::UI::Xaml::Style>());
			empty.TextWrapping(TextWrapping::Wrap);
			children.Append(empty);
			return;
		}
		for (auto const& path : m_openFiles)
		{
			Grid row;
			ColumnDefinition iconCol;
			iconCol.Width(GridLength{16, GridUnitType::Pixel});
			ColumnDefinition nameCol;
			nameCol.Width(GridLength{1, GridUnitType::Star});
			ColumnDefinition extCol;
			extCol.Width(GridLength{0, GridUnitType::Auto});
			row.ColumnDefinitions().Append(iconCol);
			row.ColumnDefinitions().Append(nameCol);
			row.ColumnDefinitions().Append(extCol);
			row.ColumnSpacing(9);

			TextBlock icon;
			icon.Text(L"\uE8A5"); // file glyph
			icon.FontFamily(Media::FontFamily(L"Segoe MDL2 Assets"));
			icon.FontSize(13);
			icon.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 22, 163, 74}));
			icon.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(icon, 0);
			row.Children().Append(icon);

			TextBlock nameBlock;
			nameBlock.Text(winrt::hstring{ BaseName(path) });
			nameBlock.FontSize(12);
			nameBlock.TextTrimming(TextTrimming::CharacterEllipsis);
			nameBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 31, 31, 30}));
			nameBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(nameBlock, 1);
			row.Children().Append(nameBlock);

			TextBlock extBlock;
			extBlock.Text(winrt::hstring{ ExtensionLabel(path) });
			extBlock.FontSize(11);
			extBlock.Foreground(Media::SolidColorBrush(Windows::UI::Color{255, 138, 138, 134}));
			extBlock.VerticalAlignment(VerticalAlignment::Center);
			Grid::SetColumn(extBlock, 2);
			row.Children().Append(extBlock);

			children.Append(row);
		}
	}

	void ShellPage::CollectFilePath(nlohmann::json const& args)
	{
		// Tools carry their target file in one of a few common arg keys.
		// We dedupe and move the most-recently-touched path to the front.
		static const std::vector<std::string> kPathKeys = { "file_path", "path", "filename" };
		for (auto const& key : kPathKeys)
		{
			if (args.contains(key) && args[key].is_string())
			{
				std::wstring wide = ToWide(args[key].get<std::string>());
				if (wide.empty())
				{
					continue;
				}
				// Dedupe: remove any existing entry, then prepend.
				m_openFiles.erase(std::remove(m_openFiles.begin(), m_openFiles.end(), wide), m_openFiles.end());
				m_openFiles.insert(m_openFiles.begin(), std::move(wide));
				if (m_openFiles.size() > 20)
				{
					m_openFiles.resize(20);
				}
				return; // one path per event
			}
		}
	}

	std::wstring ShellPage::FormatTokenCount(std::int64_t tokens)
	{
		// e.g. 0, 1234, 12.3k, 1.2M
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

	std::wstring ShellPage::BaseName(std::wstring const& path)
	{
		// Handle both / and \ separators (paths may arrive POSIX- or Win-style).
		auto pos = path.find_last_of(L"\\/");
		return pos == std::wstring::npos ? path : path.substr(pos + 1);
	}

	std::wstring ShellPage::ExtensionLabel(std::wstring const& path)
	{
		auto base = BaseName(path);
		auto pos = base.find_last_of(L'.');
		if (pos == std::wstring::npos || pos == 0)
		{
			return L"FILE";
		}
		// Uppercase the extension (without the dot) for the tag, e.g. "XAML".
		auto ext = base.substr(pos + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::towupper);
		return ext;
	}

} // namespace winrt::CodeHarness::Desktop::Views::implementation

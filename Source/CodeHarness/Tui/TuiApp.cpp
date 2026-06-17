#include "Tui/TuiApp.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/color.hpp>

#include "Cli/RunPrompt.h"
#include "Config/ConfigTypes.h"
#include "Llm/ChatProvider.h"
#include "Permission/PermissionTypes.h"
#include "Rpc/CoreApi.h"
#include "Rpc/RpcTypes.h"
#include "Tui/Components/ActivityIndicator.h"
#include "Tui/Components/Banner.h"
#include "Tui/Components/ChatPane.h"
#include "Tui/Components/CompactionIndicator.h"
#include "Tui/Components/InputField.h"
#include "Tui/Components/MessageEntry.h"
#include "Tui/Components/QueuePanel.h"
#include "Tui/Components/SidePanel.h"
#include "Tui/Components/StatusBar.h"
#include "Tui/Components/ThinkingView.h"
#include "Tui/Components/TodoPanel.h"
#include "Tui/Components/ToolCallCard.h"
#include "Tui/Components/WelcomePanel.h"
#include "Tui/EventRouter.h"
#include "Tui/TuiState.h"
#include "Tui/Utils/SlashCommands.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::tui
{

	namespace
	{

		int64_t TotalTokens(const llm::TokenUsage& u)
		{
			return u.inputOther + u.output + u.inputCacheRead + u.inputCacheCreation;
		}

		std::string_view PermissionModeLabel(config::PermissionMode mode)
		{
			switch (mode)
			{
			case config::PermissionMode::Yolo:
				return "YOLO";
			case config::PermissionMode::Auto:
				return "Auto";
			case config::PermissionMode::Manual:
			default:
				return "Manual";
			}
		}

		void PushSystemMessage(const std::shared_ptr<TuiState>& state, std::string text)
		{
			std::lock_guard lk(state->mutex);
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = std::move(text),
			});
		}

	} // namespace

	// =======================================================================
	// Construction / Destruction
	// =======================================================================

	TuiApp::TuiApp(std::unique_ptr<rpc::CoreApi> api, const cli::CliOptions& opts)
		: api(std::move(api)),
		  state(std::make_shared<TuiState>()),
		  opts(opts),
		  router(state)
	{
		auto dark = DetectDarkMode();
		state->darkMode = dark;
		state->colors = MakePalette(dark);
		state->agentStatus = agent::AgentStatus::Idle;
		state->workdir = opts.workdir;
	}

	TuiApp::~TuiApp()
	{
		stopped = true;
	}

	// =======================================================================
	// Run - entry point
	// =======================================================================

	absl::Status TuiApp::Run()
	{
		auto status = InitializeSession();
		if (!status.ok())
		{
			return status;
		}

		// Create FTXUI screen (local ownership; borrow pointer for callbacks)
		auto localScreen = ftxui::ScreenInteractive::Fullscreen();
		screen = &localScreen;

		// Build component tree and wire input handlers
		auto component = MakeLayout();

		// Start the screen loop (blocks until the user exits)
		localScreen.Loop(component);

		screen = nullptr;
		return Shutdown();
	}

	void TuiApp::PostRender()
	{
		if (!stopped && screen)
		{
			screen->PostEvent(ftxui::Event::Custom);
		}
	}

	// =======================================================================
	// Session lifecycle
	// =======================================================================

	absl::Status TuiApp::InitializeSession()
	{
		if (!opts.yolo)
		{
			api->SetApprovalCallback([this](std::string_view name,
											const nlohmann::json& args,
											std::string_view desc) {
				return OnApproval(name, args, desc);
			});
		}
		api->SetQuestionCallback([this](const tools::QuestionRequest& req) {
			return OnQuestion(req);
		});
		api->SetEventSink([this](const rpc::CoreEvent& event) {
			OnCoreEvent(event);
		});

		rpc::CreateSessionOptions sessionOpts;
		sessionOpts.workdir = opts.workdir;
		sessionOpts.title = "tui";
		sessionOpts.model = opts.model;
		sessionOpts.permissionMode = opts.yolo
										 ? config::PermissionMode::Yolo
										 : config::PermissionMode::Manual;

		std::string sessionId;
		if (!opts.sessionId.empty())
		{
			auto result = api->ResumeSession(opts.sessionId, sessionOpts);
			if (!result.ok())
			{
				return result.status();
			}
			sessionId = *result;
		}
		else if (opts.continueLast)
		{
			auto sessions = api->ListSessions(sessionOpts.workdir);
			if (sessions.ok() && !sessions->empty())
			{
				auto& latest = sessions->back();
				auto result = api->ResumeSession(latest.sessionId, sessionOpts);
				if (result.ok())
				{
					sessionId = *result;
				}
			}
		}
		if (sessionId.empty())
		{
			auto result = api->CreateSession(sessionOpts);
			if (!result.ok())
			{
				return result.status();
			}
			sessionId = *result;
		}

		{
			// Resolve the effective model name: explicit -m wins, else query CoreApi
			std::string effectiveModel = opts.model;
			if (effectiveModel.empty())
			{
				auto info = api->GetSessionInfo(sessionId);
				if (info.ok() && !info->model.empty())
				{
					effectiveModel = info->model;
				}
				else
				{
					effectiveModel = "default";
				}
			}

			std::lock_guard lk(state->mutex);
			state->sessionId = sessionId;
			state->model = effectiveModel;
			state->workdir = sessionOpts.workdir;
			state->permissionMode = sessionOpts.permissionMode;
			state->statusMessage = fmt::format("Session started: {}", sessionId);
		}

		spdlog::info("tui: session {} initialized", sessionId);
		return absl::OkStatus();
	}

	absl::Status TuiApp::Shutdown()
	{
		stopped = true;
		if (!state->sessionId.empty())
		{
			auto status = api->CloseSession(state->sessionId);
			if (!status.ok())
			{
				spdlog::warn("tui: session close failed: {}", status.message());
			}
		}
		return absl::OkStatus();
	}

	// =======================================================================
	// CoreApi callbacks (called from worker thread)
	// =======================================================================

	void TuiApp::OnCoreEvent(const rpc::CoreEvent& event)
	{
		if (stopped)
		{
			return;
		}

		router.Dispatch(event);
		PostRender();
	}

	permission::PermissionDecision TuiApp::OnApproval(std::string_view toolName,
													  const nlohmann::json& args,
													  std::string_view description)
	{
		if (stopped || opts.yolo)
		{
			return permission::PermissionDecision::Allow;
		}

		auto promise = std::make_shared<std::promise<permission::PermissionDecision>>();
		auto future = promise->get_future();

		{
			std::lock_guard lk(state->mutex);
			state->pendingApproval = PendingApproval{
				.toolName = std::string(toolName),
				.args = args,
				.description = std::string(description),
				.promise = promise,
			};
			state->activeModal = ModalKind::Approval;
		}

		PostRender();

		auto status = future.wait_for(std::chrono::minutes(5));
		if (status != std::future_status::ready)
		{
			return permission::PermissionDecision::Deny;
		}
		return future.get();
	}

	std::string TuiApp::OnQuestion(const tools::QuestionRequest& request)
	{
		if (stopped)
		{
			return "";
		}

		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();

		{
			std::lock_guard lk(state->mutex);
			state->pendingQuestion = PendingQuestion{
				.request = request,
				.promise = promise,
			};
			state->activeModal = ModalKind::Question;
		}

		PostRender();

		auto status = future.wait_for(std::chrono::minutes(5));
		if (status != std::future_status::ready)
		{
			return "";
		}
		return future.get();
	}

	// =======================================================================
	// Layout construction
	// =======================================================================

	ftxui::Component TuiApp::MakeLayout()
	{
		using namespace ftxui;

		auto main = MakeMainContainer();
		auto status = MakeStatusBar();
		auto modal = MakeModalOverlay();

		auto root = Container::Vertical({
			main,
			status,
		});

		auto app = Container::Stacked({root, modal});

		auto withInput = CatchEvent(app, [this](Event event) -> bool {
			return HandleInput(event);
		});

		return withInput;
	}

	ftxui::Component TuiApp::MakeMainContainer()
	{
		using namespace ftxui;

		// Interactive components (must live inside a focus-handling container).
		auto chat = MakeChatPane();
		auto input = MakeInputField();

		// Read-only panels (no focus handling) - their Render() output is composed
		// manually into the layout Element below.
		auto activity = ActivityIndicator::Create(state);
		auto banner = Banner::Create(state);
		auto welcome = WelcomePanel::Create(state);
		auto todo = TodoPanel::Create(state);
		auto queue = QueuePanel::Create(state);
		auto compaction = CompactionIndicator::Create(state);
		auto side = SidePanel::Create(state);

		// Stack the focusable parts (chat needs PgUp/PgDn handling; input needs
		// text entry). Other panels are decoration only and are rendered into the
		// Element tree directly.
		auto focusColumn = Container::Vertical({
			chat,
			input,
		});

		// Single Renderer composes the full main body. This avoids nesting
		// non-focusable Renderers inside focus-handling Containers (which has
		// historically triggered asserts in some FTXUI builds).
		auto body = Renderer(focusColumn, [this, focusColumn, activity, banner, welcome, todo, queue, compaction, side] {
			// chat + input own focus; their Render() output is the focus column.
			Element chatInput = focusColumn->Render() | flex;

			Element mainCol = vbox({
				welcome->Render(),
				banner->Render(),
				chatInput,
				activity->Render(),
				todo->Render(),
				queue->Render(),
				compaction->Render(),
			});

			std::lock_guard lk(state->mutex);
			if (!state->sidePanelVisible)
			{
				return std::move(mainCol);
			}
			return hbox({
				std::move(mainCol) | flex,
				side->Render(),
			});
		});

		return body;
	}

	ftxui::Component TuiApp::MakeChatPane()
	{
		using namespace ftxui;

		auto render = Renderer([this] {
			std::lock_guard lk(state->mutex);
			++spinnerFrame;
			Elements children;

			for (const auto& entry : state->transcript)
			{
				switch (entry.kind)
				{
				case TranscriptEntry::Kind::User: {
					children.push_back(MessageEntry::RenderUser(entry.text));
					break;
				}
				case TranscriptEntry::Kind::Assistant: {
					children.push_back(MessageEntry::RenderAssistant(entry.assistantText));
					break;
				}
				case TranscriptEntry::Kind::System: {
					if (MessageEntry::IsErrorSystemMessage(entry.text))
					{
						children.push_back(MessageEntry::RenderError(entry.text));
					}
					else
					{
						children.push_back(MessageEntry::RenderSystem(entry.text));
					}
					break;
				}
				case TranscriptEntry::Kind::ToolCall: {
					// Look up the tool call state (active or completed)
					const ToolCallState* tc = nullptr;
					auto ait = state->activeToolCalls.find(entry.toolCallId);
					if (ait != state->activeToolCalls.end())
					{
						tc = &ait->second;
					}
					else
					{
						auto cit = state->completedToolCalls.find(entry.toolCallId);
						if (cit != state->completedToolCalls.end())
						{
							tc = &cit->second;
						}
					}
					if (tc == nullptr)
					{
						break;
					}

					children.push_back(ToolCallCard::Render(*tc, spinnerFrame));
					break;
				}
				}
			}

		if (state->streaming && state->currentAssistantBuffer.empty())
		{
			if (!state->currentThinking.empty())
			{
				children.push_back(ThinkingView::Render(state->currentThinking, state->toolOutputExpanded));
			}
			children.push_back(hbox({
				text("  "),
				spinner(7, spinnerFrame) | color(Color::Cyan),
					text(" Thinking...") | dim | color(Color::GrayLight),
				}));
			}

			if (children.empty())
			{
				children.push_back(text(""));
			}

			return vbox(std::move(children)) | flex;
		});

		return render;
	}

	ftxui::Component TuiApp::MakeInputField()
	{
		using namespace ftxui;

		ftxui::InputOption opt;
		opt.placeholder = "Type a message... (Enter to send)";
		opt.multiline = false;
		opt.on_enter = [this] {
			if (inputContent.empty())
			{
				return;
			}
			std::string text = inputContent;
			inputContent.clear();
			history.Add(text);
			historyCursor = history.Entries().size();
			SubmitPrompt(text);
		};

		auto input = Input(&inputContent, opt);
		input->TakeFocus();

		auto withEnter = CatchEvent(input, [this](Event event) -> bool {
			if (event == Event::CtrlC && state->streaming)
			{
				(void)api->Cancel(state->sessionId);
				{
					std::lock_guard lk(state->mutex);
					state->streaming = false;
				}
				PostRender();
				return true;
			}
			// History navigation: Up/Down arrows
			if (event == Event::ArrowUp && historyCursor > 0)
			{
				if (historyCursor == history.Entries().size())
				{
					savedInput = inputContent;
				}
				--historyCursor;
				inputContent = history.Entries()[historyCursor];
				return true;
			}
			if (event == Event::ArrowDown)
			{
				if (historyCursor + 1 < history.Entries().size())
				{
					++historyCursor;
					inputContent = history.Entries()[historyCursor];
				}
				else if (historyCursor + 1 == history.Entries().size())
				{
					++historyCursor;
					inputContent = savedInput;
					savedInput.clear();
				}
				return true;
			}
			return false;
		});

		return withEnter;
	}

	ftxui::Component TuiApp::MakeStatusBar()
	{
		return StatusBar::Create(state);
	}

	ftxui::Component TuiApp::MakeModalOverlay()
	{
		using namespace ftxui;

		auto approval = MakeApprovalPanel();
		auto question = MakeQuestionDialog();
		auto help = MakeHelpDialog();
		auto sessionPicker = MakeSessionPicker();
		auto settings = MakeSettingsDialog();

		auto modalWithGuard = Container::Stacked({
			Maybe(approval, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Approval; }),
			Maybe(question, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Question; }),
			Maybe(help, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Help; }),
			Maybe(sessionPicker, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::SessionPicker; }),
			Maybe(settings, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Settings; }),
		});

		return modalWithGuard;
	}

	ftxui::Component TuiApp::MakeApprovalPanel()
	{
		using namespace ftxui;

		ButtonOption allowStyle = ButtonOption::Border();
		auto allowBtn = Button(" [A]llow ", [this] {
		permission::PermissionDecision decision = permission::PermissionDecision::Allow;
		{
			std::lock_guard lk(state->mutex);
			if (state->pendingApproval.has_value() && state->pendingApproval->promise)
			{
				state->pendingApproval->promise->set_value(decision);
				state->pendingApproval.reset();
			}
			state->activeModal = ModalKind::None;
		}
		PostRender(); }, allowStyle);

		ButtonOption denyStyle = ButtonOption::Border();
		auto denyBtn = Button(" [D]eny ", [this] {
		permission::PermissionDecision decision = permission::PermissionDecision::Deny;
		{
			std::lock_guard lk(state->mutex);
			if (state->pendingApproval.has_value() && state->pendingApproval->promise)
			{
				state->pendingApproval->promise->set_value(decision);
				state->pendingApproval.reset();
			}
			state->activeModal = ModalKind::None;
		}
		PostRender(); }, denyStyle);

		auto container = Container::Horizontal({allowBtn, denyBtn});

		// Keyboard shortcuts: A/a = allow, D/d = deny
		auto withKeys = CatchEvent(container, [this](Event event) -> bool {
			if (event == Event::Character("a") || event == Event::Character("A"))
			{
				permission::PermissionDecision decision = permission::PermissionDecision::Allow;
				std::lock_guard lk(state->mutex);
				if (state->pendingApproval.has_value() && state->pendingApproval->promise)
				{
					state->pendingApproval->promise->set_value(decision);
					state->pendingApproval.reset();
				}
				state->activeModal = ModalKind::None;
				PostRender();
				return true;
			}
			if (event == Event::Character("d") || event == Event::Character("D") || event == Event::Character("n"))
			{
				permission::PermissionDecision decision = permission::PermissionDecision::Deny;
				std::lock_guard lk(state->mutex);
				if (state->pendingApproval.has_value() && state->pendingApproval->promise)
				{
					state->pendingApproval->promise->set_value(decision);
					state->pendingApproval.reset();
				}
				state->activeModal = ModalKind::None;
				PostRender();
				return true;
			}
			return false;
		});

		auto render = Renderer(withKeys, [this, withKeys] {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::Approval || !state->pendingApproval.has_value())
			{
				return text("");
			}

			auto& pa = *state->pendingApproval;

			// Truncate args JSON for display
			std::string argsStr = pa.args.dump(2);
			if (argsStr.size() > 400)
			{
				argsStr = argsStr.substr(0, 397) + "...";
			}

			// Header with bold icon
			auto header = hbox({
				text(" ! ") | bold | color(Color::Yellow),
				text("Permission Required") | bold | color(Color::Yellow),
			});

			// Tool name + description
			Elements body;
			body.push_back(text(""));
			body.push_back(hbox({
				text("  Tool:  ") | bold | color(Color::Cyan),
				text(pa.toolName) | bold,
			}));
			if (!pa.description.empty())
			{
				body.push_back(hbox({
					text("  Desc:  ") | bold | color(Color::Cyan),
					text(pa.description),
				}));
			}
			body.push_back(text("  Args:") | bold | color(Color::Cyan));
			{
				// Split argsStr by newlines using string::find
				size_t pos = 0;
				size_t prev = 0;
				while ((pos = argsStr.find('\n', prev)) != std::string::npos)
				{
					body.push_back(text(fmt::format("    {}", argsStr.substr(prev, pos - prev))) | color(Color::GrayLight));
					prev = pos + 1;
				}
				body.push_back(text(fmt::format("    {}", argsStr.substr(prev))) | color(Color::GrayLight));
			}

			auto window = vbox({
							  header,
							  separator(),
							  vbox(std::move(body)),
							  separator(),
							  withKeys->Render() | center,
							  text(" Press A to allow, D to deny, Esc to cancel") | dim | center,
						  }) |
						  borderDouble | color(Color::Yellow) | size(WIDTH, EQUAL, 70) | center;

			return window;
		});

		return render;
	}

	ftxui::Component TuiApp::MakeQuestionDialog()
	{
		using namespace ftxui;

		// Use a Radiobox for option selection
		auto selected = std::make_shared<int>(0);
		std::vector<std::string> options;

		RadioboxOption opt;
		auto radiobox = Radiobox(&options, selected.get(), opt);

		// Free-form input option (when allowFreeform is true)
		auto freeInput = std::make_shared<std::string>();
		InputOption inputOpt;
		inputOpt.placeholder = "Type your answer...";
		auto input = Input(freeInput.get(), inputOpt);

		auto container = Container::Vertical({radiobox, input});

		auto render = Renderer(container, [this, container, &options, selected, freeInput, input, radiobox] {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::Question || !state->pendingQuestion.has_value())
			{
				return text("");
			}

			auto& pq = *state->pendingQuestion;

			// Sync the options vector (only when changed)
			if (options.size() != pq.request.options.size())
			{
				options = pq.request.options;
				if (pq.request.allowFreeform)
				{
					options.push_back("(custom answer)");
				}
				*selected = 0;
			}

			auto header = hbox({
				text(" ? ") | bold | color(Color::Cyan),
				text("Question") | bold | color(Color::Cyan),
			});

			Elements body;
			body.push_back(text(""));
			body.push_back(text(pq.request.question) | bold);
			body.push_back(text(""));

			// Show radiobox for options
			body.push_back(radiobox->Render());

			// Show free-form input if (a) freeform allowed AND user picked last option, or (b) no options
			bool showInput = pq.request.allowFreeform &&
							 (options.empty() || *selected == static_cast<int>(options.size()) - 1);
			if (showInput)
			{
				body.push_back(text(""));
				body.push_back(text("Custom answer:") | dim);
				body.push_back(input->Render());
			}

			auto window = vbox({
							  header,
							  separator(),
							  vbox(std::move(body)),
							  separator(),
							  text(" Enter to confirm, Esc to cancel") | dim | center,
						  }) |
						  border | color(Color::Cyan) | size(WIDTH, EQUAL, 70) | center;

			return window;
		});

		// Wire Enter to confirm
		auto withEnter = CatchEvent(render, [this, selected, freeInput, &options](Event event) -> bool {
			if (event == Event::Return)
			{
				std::string answer;
				if (!options.empty() && *selected < static_cast<int>(options.size()))
				{
					if (state->pendingQuestion.has_value() && state->pendingQuestion->request.allowFreeform &&
						*selected == static_cast<int>(options.size()) - 1)
					{
						answer = *freeInput;
					}
					else
					{
						answer = options[*selected];
					}
				}
				else
				{
					answer = *freeInput;
				}
				std::lock_guard lk(state->mutex);
				if (state->pendingQuestion.has_value() && state->pendingQuestion->promise)
				{
					state->pendingQuestion->promise->set_value(answer);
					state->pendingQuestion.reset();
				}
				state->activeModal = ModalKind::None;
				*freeInput = "";
				PostRender();
				return true;
			}
			return false;
		});

		return withEnter;
	}

	ftxui::Component TuiApp::MakeHelpDialog()
	{
		using namespace ftxui;
		auto render = Renderer([this] {
			if (state->activeModal != ModalKind::Help)
			{
				return text("");
			}
			return vbox({
					   text(" Help ") | bold | center,
					   separator(),
					   text("  Keyboard:") | bold,
					   text("    Enter       Send message"),
					   text("    Up/Down     Navigate input history"),
					   text("    Ctrl+C      Cancel streaming"),
					   text("    Ctrl+B      Toggle side panel"),
					   text("    Ctrl+L      Redraw screen"),
					   text("    Escape      Close dialog"),
					   separator(),
					   text("  Slash commands:") | bold,
					   text("    /help, /h, /?  Show this help"),
					   text("    /clear, /new   Clear context or start fresh"),
					   text("    /compact        Compact context now"),
					   text("    /fork           Fork current session"),
					   text("    /rename, /title Set or rename session title"),
					   text("    /status         Show session/model/mode"),
					   text("    /version        Show CodeHarness version"),
					   text("    /exit, /quit, /q Quit"),
					   text("    /model          Pick model from list"),
					   text("    /model X        Switch to model X"),
					   text("    /sessions, /resume Pick session to resume"),
					   text("    /mode, /yolo    Toggle YOLO/Manual"),
					   text("    /permission     Permission selector"),
					   text("    /settings, /config Settings"),
					   text("    /usage          Show usage summary"),
					   text("    /login, /logout Account access"),
					   text("    /mcp, /plugins, /tasks, /feedback"),
					   text("    /reload, /reload-tui, /export"),
					   text("    /plan, /goal, /swarm, /btw, /undo"),
					   separator(),
					   text("  Press Escape to close") | dim,
				   }) |
				   border | center;
		});
		return render;
	}

	ftxui::Component TuiApp::MakeSessionPicker()
	{
		using namespace ftxui;

		auto selected = std::make_shared<int>(0);
		std::vector<std::string> displayLines; // session titles for display
		std::vector<std::string> sessionIds;   // matching session IDs

		RadioboxOption opt;
		auto radiobox = Radiobox(&displayLines, selected.get(), opt);

		auto render = Renderer(radiobox, [this, radiobox, &displayLines, &sessionIds, selected] {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::SessionPicker)
			{
				return text("");
			}

			// Build display lines from available sessions
			if (displayLines.size() != state->availableSessions.size())
			{
				displayLines.clear();
				sessionIds.clear();
				for (const auto& s : state->availableSessions)
				{
					std::string title = s.title.empty() ? "(untitled)" : s.title;
					if (title.size() > 50)
						title = title.substr(0, 47) + "...";
					displayLines.push_back(fmt::format("{} [{}]", title, s.sessionId.substr(0, 8)));
					sessionIds.push_back(s.sessionId);
				}
				*selected = 0;
			}

			Elements body;
			body.push_back(text("Sessions") | bold | color(Color::Cyan));
			body.push_back(text(""));
			if (displayLines.empty())
			{
				body.push_back(text("No sessions found.") | dim);
			}
			else
			{
				body.push_back(radiobox->Render());
			}
			body.push_back(text(""));
			body.push_back(text("Enter to resume, Esc to cancel") | dim);

			return vbox(std::move(body)) | border | size(WIDTH, EQUAL, 70) | center;
		});

		auto withEnter = CatchEvent(render, [this, &sessionIds, selected](Event event) -> bool {
			if (event == Event::Return)
			{
				if (*selected < static_cast<int>(sessionIds.size()))
				{
					auto targetId = sessionIds[*selected];
					// Resume in a background thread (Prompt is blocking)
					std::thread([this, targetId] {
						rpc::CreateSessionOptions so;
						so.workdir = opts.workdir;
						so.title = "tui";
						so.model = opts.model;
						so.permissionMode = opts.yolo ? config::PermissionMode::Yolo : config::PermissionMode::Manual;
						auto r = api->ResumeSession(targetId, so);
						if (r.ok())
						{
							std::lock_guard lk(state->mutex);
							state->sessionId = *r;
							state->transcript.clear();
							state->transcript.push_back({
								.kind = TranscriptEntry::Kind::System,
								.text = fmt::format("Resumed session: {}", *r),
							});
						}
					}).detach();
				}
				std::lock_guard lk(state->mutex);
				state->activeModal = ModalKind::None;
				PostRender();
				return true;
			}
			return false;
		});

		return withEnter;
	}

	ftxui::Component TuiApp::MakeSettingsDialog()
	{
		using namespace ftxui;

		auto selected = std::make_shared<int>(0);
		std::vector<std::string> modelNames;

		RadioboxOption opt;
		auto radiobox = Radiobox(&modelNames, selected.get(), opt);

		auto render = Renderer(radiobox, [this, radiobox, &modelNames, selected] {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::Settings)
			{
				return text("");
			}

			// Build model list lazily
			if (modelNames.empty() && !state->availableModels.empty())
			{
				modelNames = state->availableModels;
				*selected = 0;
			}

			Elements body;
			body.push_back(text("Settings - Model Selection") | bold | color(Color::Cyan));
			body.push_back(text(""));
			body.push_back(text(fmt::format("Current: {}", state->model)) | dim);
			body.push_back(text(""));
			if (modelNames.empty())
			{
				body.push_back(text("(no models configured)") | dim);
			}
			else
			{
				body.push_back(radiobox->Render());
			}
			body.push_back(text(""));
			body.push_back(text("Enter to apply, Esc to cancel") | dim);

			return vbox(std::move(body)) | border | size(WIDTH, EQUAL, 70) | center;
		});

		auto withEnter = CatchEvent(render, [this, &modelNames, selected](Event event) -> bool {
			if (event == Event::Return)
			{
				if (*selected < static_cast<int>(modelNames.size()))
				{
					auto target = modelNames[*selected];
					std::thread([this, target] {
						(void)api->SetModel(state->sessionId, target);
						std::lock_guard lk(state->mutex);
						state->model = target;
					}).detach();
				}
				std::lock_guard lk(state->mutex);
				state->activeModal = ModalKind::None;
				PostRender();
				return true;
			}
			return false;
		});

		return withEnter;
	}

	// =======================================================================
	// Input handling
	// =======================================================================

	bool TuiApp::HandleInput(ftxui::Event event)
	{
		if (event == ftxui::Event::Escape)
		{
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::None)
			{
				if (state->pendingApproval.has_value() && state->pendingApproval->promise)
				{
					state->pendingApproval->promise->set_value(permission::PermissionDecision::Deny);
					state->pendingApproval.reset();
				}
				if (state->pendingQuestion.has_value() && state->pendingQuestion->promise)
				{
					state->pendingQuestion->promise->set_value("");
					state->pendingQuestion.reset();
				}
				state->activeModal = ModalKind::None;
				PostRender();
				return true;
			}
			return true;
		}

	// Ctrl+B toggles the side panel (Kimi-style shortcut).
	if (event == ftxui::Event::CtrlB)
	{
		std::lock_guard lk(state->mutex);
		state->sidePanelVisible = !state->sidePanelVisible;
		PostRender();
		return true;
	}

	if (event == ftxui::Event::Character("o") || event == ftxui::Event::Character("O") ||
		event == ftxui::Event::Character("\x0F"))
	{
		std::lock_guard lk(state->mutex);
		ApplyToolOutputExpanded(*state, !state->toolOutputExpanded);
		state->statusMessage = state->toolOutputExpanded ? "Expanded tool output" : "Collapsed tool output";
		PostRender();
		return true;
	}

	// Ctrl+L clears the screen (keeps transcript).
	if (event == ftxui::Event::CtrlL)
		{
			PostRender();
			return true;
		}

		return false;
	}

	bool TuiApp::HandleSlashCommand(std::string_view cmd)
	{
		auto parsed = SlashCommands::Parse(cmd);
		if (!parsed.has_value())
		{
			return false;
		}

		const auto* command = SlashCommands::Find(parsed->name);
		if (command == nullptr)
		{
			return false;
		}

		const std::string name = command->name;
		const std::string args = parsed->args;

		if (name == "help")
		{
			std::lock_guard lk(state->mutex);
			state->activeModal = ModalKind::Help;
			PostRender();
			return true;
		}
		if (name == "new")
		{
			(void)api->ClearContext(state->sessionId);
			std::lock_guard lk(state->mutex);
			state->transcript.clear();
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = args.empty() ? "Context cleared." : fmt::format("Context cleared. New-session title is not implemented yet: {}", args),
			});
			PostRender();
			return true;
		}
		if (name == "exit")
		{
			if (screen)
			{
				screen->Exit();
			}
			else
			{
				stopped = true;
			}
			return true;
		}
		if (name == "compact")
		{
			std::thread([this] {
				auto status = api->CompactNow(state->sessionId);
				std::lock_guard lk(state->mutex);
				if (status.ok())
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = "Context compacted.",
					});
				}
				else
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Error: compaction failed: {}", status.message()),
					});
				}
				PostRender();
			}).detach();
			return true;
		}
		if (name == "fork")
		{
			std::thread([this, title = args] {
				auto result = api->ForkSession(state->sessionId, title);
				std::lock_guard lk(state->mutex);
				if (result.ok())
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Forked to new session: {}", *result),
					});
				}
				else
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Error: fork failed: {}", result.status().message()),
					});
				}
				PostRender();
			}).detach();
			return true;
		}
		if (name == "status")
		{
			std::lock_guard lk(state->mutex);
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = fmt::format(
					"session={} model={} mode={}",
					state->sessionId.empty() ? "-" : state->sessionId,
					state->model.empty() ? "-" : state->model,
					PermissionModeLabel(state->permissionMode)),
			});
			PostRender();
			return true;
		}
		if (name == "usage")
		{
			std::lock_guard lk(state->mutex);
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = fmt::format("usage={} tokens", TotalTokens(state->lastUsage)),
			});
			PostRender();
			return true;
		}
		if (name == "version")
		{
			PushSystemMessage(state, "CodeHarness v0.1.0");
			PostRender();
			return true;
		}
		if (name == "title")
		{
			if (args.empty())
			{
				PushSystemMessage(state, "Usage: /title <title>");
				PostRender();
				return true;
			}
			std::thread([this, title = args] {
				auto status = api->RenameSession(state->sessionId, title);
				std::lock_guard lk(state->mutex);
				if (status.ok())
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Renamed session to: {}", title),
					});
				}
				else
				{
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Error: rename failed: {}", status.message()),
					});
				}
				PostRender();
			}).detach();
			return true;
		}
		if (name == "sessions")
		{
			std::thread([this] {
				auto sessions = api->ListSessions(opts.workdir);
				if (sessions.ok())
				{
					std::lock_guard lk(state->mutex);
					state->availableSessions = std::move(*sessions);
					state->activeModal = ModalKind::SessionPicker;
				}
				PostRender();
			}).detach();
			return true;
		}
		if (name == "model")
		{
			if (!args.empty())
			{
				auto status = api->SetModel(state->sessionId, args);
				if (status.ok())
				{
					std::lock_guard lk(state->mutex);
					state->model = args;
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Model switched to: {}", args),
					});
				}
				else
				{
					PushSystemMessage(state, fmt::format("Error: {}", status.message()));
				}
				PostRender();
				return true;
			}
			std::thread([this] {
				auto models = api->ListModels();
				if (models.ok())
				{
					std::lock_guard lk(state->mutex);
					state->availableModels.clear();
					for (const auto& m : *models)
					{
						state->availableModels.push_back(m.alias);
					}
					state->activeModal = ModalKind::Settings;
				}
				PostRender();
			}).detach();
			return true;
		}
		if (name == "settings")
		{
			std::lock_guard lk(state->mutex);
			state->activeModal = ModalKind::Settings;
			PostRender();
			return true;
		}
		if (name == "yolo")
		{
			config::PermissionMode newMode;
			{
				std::lock_guard lk(state->mutex);
				newMode = state->permissionMode == config::PermissionMode::Yolo
							  ? config::PermissionMode::Manual
							  : config::PermissionMode::Yolo;
			}
			(void)api->SetPermissionMode(state->sessionId, newMode);
			{
				std::lock_guard lk(state->mutex);
				state->permissionMode = newMode;
				state->transcript.push_back({
					.kind = TranscriptEntry::Kind::System,
					.text = fmt::format("Mode: {}", PermissionModeLabel(newMode)),
				});
			}
			PostRender();
			return true;
		}
		if (name == "auto")
		{
			(void)api->SetPermissionMode(state->sessionId, config::PermissionMode::Auto);
			std::lock_guard lk(state->mutex);
			state->permissionMode = config::PermissionMode::Auto;
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = "Mode: Auto (falls back to Manual until policy rules are implemented)",
			});
			PostRender();
			return true;
		}

		PushSystemMessage(state, fmt::format("/{0} is recognized from Kimi Code but is not implemented in CodeHarness TUI yet.", name));
		PostRender();
		return true;
	}

	void TuiApp::SubmitPrompt(std::string text)
	{
		std::string sessionId;
		{
			std::lock_guard lk(state->mutex);
			sessionId = state->sessionId;
		}

		if (text.empty() || sessionId.empty())
		{
			spdlog::debug("tui: ignoring prompt submit text_len={} session_empty={}", text.size(), sessionId.empty());
			return;
		}

		spdlog::debug("tui: submit prompt session={} text_len={}", sessionId, text.size());

		if (text[0] == '/')
		{
			if (HandleSlashCommand(text))
			{
				return;
			}
		}

		{
			std::lock_guard lk(state->mutex);
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::User,
				.text = text,
			});
			state->streaming = true;
			state->currentAssistantBuffer.clear();
		}
		PostRender();

		// Detach a thread to run the prompt without blocking the UI.
		std::thread([this, sessionId, text] {
			try
			{
				spdlog::debug("tui: prompt thread start session={} text_len={}", sessionId, text.size());
				auto result = api->Prompt(sessionId, text);
				if (!result.ok())
				{
					spdlog::error("tui: prompt error: {}", result.status().message());
					{
						std::lock_guard lk(state->mutex);
						state->lastError = std::string(result.status().message());
						state->transcript.push_back({
							.kind = TranscriptEntry::Kind::System,
							.text = fmt::format("Error: {}", result.status().message()),
						});
						state->streaming = false;
					}
					PostRender();
				}
				else
				{
					spdlog::debug("tui: prompt completed session={} steps={}", sessionId, result->stepsExecuted);
					{
						std::lock_guard lk(state->mutex);
						state->lastUsage = result->usage;
						state->streaming = false;
					}
					PostRender();
				}
			}
			catch (const std::exception& e)
			{
				spdlog::error("tui: prompt threw exception type={} what={}", typeid(e).name(), e.what());
				{
					std::lock_guard lk(state->mutex);
					state->lastError = e.what();
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = fmt::format("Error: {}", e.what()),
					});
					state->streaming = false;
				}
				PostRender();
			}
			catch (...)
			{
				spdlog::error("tui: prompt threw unknown exception");
				{
					std::lock_guard lk(state->mutex);
					state->lastError = "unknown prompt exception";
					state->transcript.push_back({
						.kind = TranscriptEntry::Kind::System,
						.text = "Error: unknown prompt exception",
					});
					state->streaming = false;
				}
				PostRender();
			}
		}).detach();
	}

	// =======================================================================
	// Free: Top-level Run function
	// =======================================================================

	absl::Status Run(host::Host* host, llm::HttpClient* http, const cli::CliOptions& opts)
	{
		rpc::CoreApiConfig cfg;
		cfg.host = host;
		cfg.http = http;

		cfg.providerResolver = [host, http, &opts](std::string_view) -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			auto resolved = rpc::ResolveProviderFromConfig(host, http, opts.model);
			if (!resolved.ok())
			{
				return resolved.status();
			}
			// Leak the unique_ptr intentionally. Provider lifetime = process.
			auto ptr = resolved->first.release();
			return std::pair<llm::ChatProvider*, std::string>{ptr, resolved->second};
		};

		auto api = std::make_unique<rpc::CoreApi>(std::move(cfg));
		auto app = std::make_unique<TuiApp>(std::move(api), opts);
		return app->Run();
	}

} // namespace codeharness::tui

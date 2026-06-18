#include "Tui/TuiApp.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

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
#include "Tui/Components/ApprovalPanel.h"
#include "Tui/Components/Banner.h"
#include "Tui/Components/ChatPane.h"
#include "Tui/Components/CompactionIndicator.h"
#include "Tui/Components/ModalOverlay.h"
#include "Tui/Components/HelpDialog.h"
#include "Tui/Components/InputField.h"
#include "Tui/Components/MessageEntry.h"
#include "Tui/Components/QuestionDialog.h"
#include "Tui/Components/QueuePanel.h"
#include "Tui/Components/SessionPicker.h"
#include "Tui/Components/SettingsDialog.h"
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

		auto gatedRoot = Maybe(root, [this] {
			std::lock_guard lk(state->mutex);
			return state->activeModal == ModalKind::None;
		});
		auto app = Container::Stacked({gatedRoot, modal});

		auto withInput = CatchEvent(app, [this](Event event) -> bool {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::None)
			{
				if (event == Event::Escape || event == Event::CtrlC || event == Event::CtrlD)
				{
					return true;
				}
			}
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
		opt.placeholder = "Type a message... (Enter to send, Shift+Enter newline)";
		opt.multiline = false;
		opt.on_change = [this] {
			historyNav.cursor = history.Entries().size();
		};

		auto submitCurrent = [this] {
			if (inputContent.empty())
			{
				return;
			}
			std::string text = inputContent;
			inputContent.clear();
			history.Add(text);
			historyNav.cursor = history.Entries().size();
			historyNav.savedInput.clear();
			SubmitPrompt(text);
		};
		opt.on_enter = submitCurrent;

		auto input = Input(&inputContent, opt);
		input->TakeFocus();

		auto withEnter = CatchEvent(input, [this, submitCurrent](Event event) -> bool {
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

			if (event == Event::Tab)
			{
				auto slash = BuildSlashSuggestions(inputContent, 1);
				if (!slash.empty())
				{
					inputContent = "/" + slash.front().command.name + " ";
					return true;
				}
				auto mention = FindFileMentionQuery(inputContent, inputContent.size());
				if (mention.has_value())
				{
					auto files = BuildFileMentionSuggestions(opts.workdir, mention->prefix, 1);
					if (!files.empty())
					{
						inputContent = ApplyFileMentionCompletion(inputContent, *mention, files.front().insertText);
						return true;
					}
				}
			}

			if (IsShiftEnterInputSequence(event.input()))
			{
				inputContent.push_back('\n');
				return true;
			}

			if (event == Event::Return || event == Event::Character("\n"))
			{
				auto slash = BuildSlashSuggestions(inputContent, 1);
				if (!slash.empty() && inputContent == "/" + slash.front().command.name)
				{
					// Exact command: fall through to normal submit so /help opens immediately.
				}
				else if (!slash.empty())
				{
					inputContent = "/" + slash.front().command.name + " ";
					return true;
				}
				else if (auto mention = FindFileMentionQuery(inputContent, inputContent.size()); mention.has_value())
				{
					auto files = BuildFileMentionSuggestions(opts.workdir, mention->prefix, 1);
					if (!files.empty())
					{
						inputContent = ApplyFileMentionCompletion(inputContent, *mention, files.front().insertText);
						return true;
					}
				}
				if (auto action = SubmitAction(false, inputContent.empty()); action == ComposerSubmitAction::Submit)
				{
					submitCurrent();
					return true;
				}
				return true;
			}

			// History navigation: Up/Down arrows
			if (event == Event::ArrowUp)
			{
				return ApplyHistoryUp(history.Entries(), historyNav, inputContent);
			}
			if (event == Event::ArrowDown)
			{
				return ApplyHistoryDown(history.Entries(), historyNav, inputContent);
			}
			return false;
		});

		auto render = Renderer(withEnter, [this, withEnter] {
			Elements rows;
			auto slash = BuildSlashSuggestions(inputContent, 6);
			if (!slash.empty())
			{
				for (const auto& suggestion : slash)
				{
					rows.push_back(text("  " + suggestion.display) | dim | color(Color::GrayLight));
				}
			}
			else if (auto mention = FindFileMentionQuery(inputContent, inputContent.size()); mention.has_value())
			{
				auto files = BuildFileMentionSuggestions(opts.workdir, mention->prefix, 6);
				for (const auto& file : files)
				{
					rows.push_back(text("  " + file.display + (file.isDirectory ? "  dir" : "")) | dim | color(Color::GrayLight));
				}
			}
			rows.push_back(withEnter->Render());
			return vbox(std::move(rows));
		});

		return render;
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
		auto modelPicker = MakeModelPicker();
		auto sessionPicker = MakeSessionPicker();
		auto settings = MakeSettingsDialog();

		auto modalWithGuard = Container::Tab({
			Maybe(approval, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Approval; }),
			Maybe(question, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Question; }),
			Maybe(help, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Help; }),
			Maybe(modelPicker, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::ModelPicker; }),
			Maybe(sessionPicker, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::SessionPicker; }),
			Maybe(settings, [this] { std::lock_guard lk(state->mutex); return state->activeModal == ModalKind::Settings; }),
		}, &activeModalIndex);

		return ModalOverlay::Create(
			modalWithGuard,
			ModalOverlayOptions{.visible = [this] {
				std::lock_guard lk(state->mutex);
				switch (state->activeModal)
				{
				case ModalKind::Approval:
					activeModalIndex = 0;
					return true;
				case ModalKind::Question:
					activeModalIndex = 1;
					return true;
				case ModalKind::Help:
					activeModalIndex = 2;
					return true;
				case ModalKind::ModelPicker:
					activeModalIndex = 3;
					return true;
				case ModalKind::SessionPicker:
					activeModalIndex = 4;
					return true;
				case ModalKind::Settings:
					activeModalIndex = 5;
					return true;
				case ModalKind::None:
				default:
					return false;
				}
			}});
	}

	ftxui::Component TuiApp::MakeApprovalPanel()
	{
		return ApprovalPanel::Create(ApprovalPanelOptions{
			.request = [this]() -> std::optional<ApprovalPanelRequest> {
				std::lock_guard lk(state->mutex);
				if (!state->pendingApproval.has_value())
				{
					return std::nullopt;
				}
				const auto& approval = *state->pendingApproval;
				return ApprovalPanelRequest{.toolName = approval.toolName, .args = approval.args, .description = approval.description};
			},
			.onResponse = [this](ApprovalPanelResponse response) {
				{
					std::lock_guard lk(state->mutex);
					if (state->pendingApproval.has_value() && state->pendingApproval->promise)
					{
						state->pendingApproval->promise->set_value(response.decision);
						state->pendingApproval.reset();
					}
					state->activeModal = ModalKind::None;
				}
				PostRender();
			},
			.onToggleToolOutput = [this] {
				{
					std::lock_guard lk(state->mutex);
					ApplyToolOutputExpanded(*state, !state->toolOutputExpanded);
					state->statusMessage = state->toolOutputExpanded ? "Expanded tool output" : "Collapsed tool output";
				}
				PostRender();
			},
		});
	}

	ftxui::Component TuiApp::MakeQuestionDialog()
	{
		return QuestionDialog::Create(QuestionDialogOptions{
			.request = [this]() -> std::optional<tools::QuestionRequest> {
				std::lock_guard lk(state->mutex);
				if (!state->pendingQuestion.has_value())
				{
					return std::nullopt;
				}
				return state->pendingQuestion->request;
			},
			.onAnswer = [this](std::string answer) {
				{
					std::lock_guard lk(state->mutex);
					if (state->pendingQuestion.has_value() && state->pendingQuestion->promise)
					{
						state->pendingQuestion->promise->set_value(std::move(answer));
						state->pendingQuestion.reset();
					}
					state->activeModal = ModalKind::None;
				}
				PostRender();
			},
			.onToggleToolOutput = [this] {
				{
					std::lock_guard lk(state->mutex);
					ApplyToolOutputExpanded(*state, !state->toolOutputExpanded);
					state->statusMessage = state->toolOutputExpanded ? "Expanded tool output" : "Collapsed tool output";
				}
				PostRender();
			},
		});
	}

	ftxui::Component TuiApp::MakeHelpDialog()
	{
		return HelpDialog::Create(HelpDialogOptions{
			.commands = SlashCommands::All(),
			.onClose = [this] {
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
				}
				PostRender();
			},
			.maxVisible = 24,
		});
	}

	ftxui::Component TuiApp::MakeModelPicker()
	{
		using namespace ftxui;

		auto selected = std::make_shared<int>(0);
		auto modelNames = std::make_shared<std::vector<std::string>>();
		RadioboxOption opt;
		auto radiobox = Radiobox(modelNames.get(), selected.get(), opt);

		auto render = Renderer(radiobox, [this, radiobox, modelNames, selected] {
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::ModelPicker)
			{
				return text("");
			}
			*modelNames = state->availableModels;
			if (*selected >= static_cast<int>(modelNames->size()))
			{
				*selected = 0;
			}

			Elements body;
			body.push_back(text(" Model ") | bold | color(Color::Cyan));
			body.push_back(text(" Current: " + state->model) | dim);
			body.push_back(text(""));
			if (modelNames->empty())
			{
				body.push_back(text("No models configured.") | dim);
			}
			else
			{
				body.push_back(radiobox->Render());
			}
			body.push_back(text(""));
			body.push_back(text("Enter select, Esc cancel") | dim);
			return vbox(std::move(body)) | border | size(WIDTH, LESS_THAN, 90) | center;
		});

		return CatchEvent(render, [this, modelNames, selected](Event event) -> bool {
			if (event == Event::Escape)
			{
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
				}
				PostRender();
				return true;
			}
			if (event == Event::Return)
			{
				if (*selected >= 0 && *selected < static_cast<int>(modelNames->size()))
				{
					auto target = (*modelNames)[static_cast<std::size_t>(*selected)];
					std::thread([this, target] {
						auto status = api->SetModel(state->sessionId, target);
						std::lock_guard lk(state->mutex);
						if (status.ok())
						{
							state->model = target;
							state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Model switched to: {}", target)});
						}
						else
						{
							state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Error: {}", status.message())});
						}
						state->activeModal = ModalKind::None;
						PostRender();
					}).detach();
				}
				return true;
			}
			return false;
		});
	}

	ftxui::Component TuiApp::MakeSessionPicker()
	{
		return SessionPicker::Create(SessionPickerOptions{
			.sessions = [this] {
				std::lock_guard lk(state->mutex);
				return state->availableSessions;
			},
			.currentSessionId = [this] {
				std::lock_guard lk(state->mutex);
				return state->sessionId;
			},
			.scope = [this] {
				std::lock_guard lk(state->mutex);
				return state->sessionPickerAllScope ? SessionPickerScope::All : SessionPickerScope::Cwd;
			},
			.onSelect = [this](session::SessionInfo info) {
				std::thread([this, targetId = info.sessionId] {
					rpc::CreateSessionOptions so;
					so.workdir = opts.workdir;
					so.title = "tui";
					so.model = opts.model;
					so.permissionMode = opts.yolo ? config::PermissionMode::Yolo : config::PermissionMode::Manual;
					auto r = api->ResumeSession(targetId, so);
					std::lock_guard lk(state->mutex);
					if (r.ok())
					{
						state->sessionId = *r;
						state->transcript.clear();
						state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Resumed session: {}", *r)});
					}
					else
					{
						state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Error: resume failed: {}", r.status().message())});
					}
					state->activeModal = ModalKind::None;
					PostRender();
				}).detach();
			},
			.onCancel = [this] {
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
				}
				PostRender();
			},
			.onToggleScope = [this] {
				{
					std::lock_guard lk(state->mutex);
					state->sessionPickerAllScope = !state->sessionPickerAllScope;
					state->statusMessage = state->sessionPickerAllScope
						? "All-session scope is not implemented yet; showing current workspace sessions"
						: "Showing current workspace sessions";
				}
				PostRender();
			},
			.onCtrlC = [this] { (void)api->Cancel(state->sessionId); },
			.onCtrlD = [this] {
				if (screen)
				{
					screen->Exit();
				}
			},
		});
	}

	ftxui::Component TuiApp::MakeSettingsDialog()
	{
		return SettingsDialog::Create(SettingsDialogOptions{
			.currentModel = [this] {
				std::lock_guard lk(state->mutex);
				return state->model;
			},
			.currentPermissionMode = [this] {
				std::lock_guard lk(state->mutex);
				return state->permissionMode;
			},
			.onSelect = [this](SettingsSelection selection) {
				if (selection == SettingsSelection::Model)
				{
					std::thread([this] {
						auto models = api->ListModels();
						std::lock_guard lk(state->mutex);
						if (models.ok())
						{
							state->availableModels.clear();
							for (const auto& m : *models)
							{
								state->availableModels.push_back(m.alias);
							}
							state->activeModal = ModalKind::ModelPicker;
						}
						else
						{
							state->activeModal = ModalKind::None;
							state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Error: {}", models.status().message())});
						}
						PostRender();
					}).detach();
					return;
				}
				if (selection == SettingsSelection::Permission)
				{
					config::PermissionMode newMode;
					{
						std::lock_guard lk(state->mutex);
						newMode = state->permissionMode == config::PermissionMode::Yolo ? config::PermissionMode::Manual : config::PermissionMode::Yolo;
						state->permissionMode = newMode;
						state->activeModal = ModalKind::None;
						state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("Mode: {}", PermissionModeLabel(newMode))});
					}
					(void)api->SetPermissionMode(state->sessionId, newMode);
					PostRender();
					return;
				}
				if (selection == SettingsSelection::Usage)
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
					state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = fmt::format("usage={} tokens", TotalTokens(state->lastUsage))});
					PostRender();
					return;
				}
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
					state->transcript.push_back({.kind = TranscriptEntry::Kind::System, .text = "This settings page is not implemented yet."});
				}
				PostRender();
			},
			.onCancel = [this] {
				{
					std::lock_guard lk(state->mutex);
					state->activeModal = ModalKind::None;
				}
				PostRender();
			},
		});
	}
	// =======================================================================
	// Input handling
	// =======================================================================

	bool TuiApp::HandleInput(ftxui::Event event)
	{
		{
			std::lock_guard lk(state->mutex);
			if (state->activeModal != ModalKind::None)
			{
				return false;
			}
		}

		if (event == ftxui::Event::Escape)
		{
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

	if (event == ftxui::Event::CtrlO)
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
					state->activeModal = ModalKind::ModelPicker;
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


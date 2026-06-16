#include "Tui/TuiApp.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
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
#include "Tui/Components/ChatPane.h"
#include "Tui/Components/InputField.h"
#include "Tui/Components/StatusBar.h"
#include "Tui/EventRouter.h"
#include "Tui/Renderers/MarkdownRenderer.h"
#include "Tui/TuiState.h"
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

} // namespace

// ═══════════════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════════════

TuiApp::TuiApp(std::unique_ptr<rpc::CoreApi> api, const cli::CliOptions& opts)
	: api(std::move(api)),
	  opts(opts),
	  router(state)
{
	auto dark = DetectDarkMode();
	state = std::make_shared<TuiState>();
	state->darkMode = dark;
	state->colors = MakePalette(dark);
	state->agentStatus = agent::AgentStatus::Idle;
}

TuiApp::~TuiApp()
{
	stopped = true;
}

// ═══════════════════════════════════════════════════════════════════════
// Run - entry point
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// Session lifecycle
// ═══════════════════════════════════════════════════════════════════════

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
		state->permissionMode = sessionOpts.permissionMode;
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = fmt::format("Session started: {}", sessionId),
		});
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

// ═══════════════════════════════════════════════════════════════════════
// CoreApi callbacks (called from worker thread)
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
// Layout construction
// ═══════════════════════════════════════════════════════════════════════

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
	return Container::Vertical({
		MakeChatPane(),
		MakeInputField(),
	});
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
				case TranscriptEntry::Kind::User:
				{
					// Bullet + bold "You" label + dim message text
					children.push_back(hbox({
						text("> ") | bold | color(Color::Green),
						text("You") | bold | color(Color::Green),
						text(": ") | dim,
						text(entry.text),
					}));
					break;
				}
				case TranscriptEntry::Kind::Assistant:
				{
					children.push_back(MarkdownRenderer::Render(entry.assistantText));
					break;
				}
				case TranscriptEntry::Kind::System:
				{
					// Distinguish errors (text starts with "Error:" or contains "denied")
					// from ordinary status messages
					bool isError = entry.text.rfind("Error:", 0) == 0 ||
								   entry.text.find("denied") != std::string::npos ||
								   entry.text.find("failed") != std::string::npos;
					if (isError)
					{
						children.push_back(hbox({
							text(" ! ") | bold | color(Color::Red),
							text(entry.text) | color(Color::Red),
						}));
					}
					else
					{
						children.push_back(hbox({
							text(" - ") | dim | color(Color::GrayDark),
							text(entry.text) | dim | color(Color::GrayLight),
						}));
					}
					break;
				}
				case TranscriptEntry::Kind::ToolCall:
				{
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

					// Status icon and color
					Color statusColor;
					Element iconEl;
					if (tc->status == "running")
					{
						statusColor = Color::Yellow;
						iconEl = spinner(7, spinnerFrame) | bold | color(statusColor);
					}
					else if (tc->status == "error")
					{
						statusColor = Color::Red;
						iconEl = text("x") | bold | color(statusColor);
					}
					else
					{
						statusColor = Color::Green;
						iconEl = text("+") | bold | color(statusColor);
					}

					// Brief args preview: try common fields (path/command/pattern)
					std::string argsPreview;
					if (tc->args.is_object())
					{
						for (const char* key : {"path", "file_path", "file", "command", "pattern", "query", "url"})
						{
							if (tc->args.contains(key))
							{
								const auto& v = tc->args[key];
								if (v.is_string())
								{
									argsPreview = v.get<std::string>();
									break;
								}
							}
						}
						if (argsPreview.empty() && !tc->args.empty())
						{
							argsPreview = tc->args.begin().value().dump();
						}
					}
					if (argsPreview.size() > 60)
					{
						argsPreview = argsPreview.substr(0, 57) + "...";
					}

					// Header line: "  + ToolName  args..."
					Elements header;
					header.push_back(text("  "));
					header.push_back(std::move(iconEl));
					header.push_back(text(" "));
					header.push_back(text(tc->name) | bold);
					if (!argsPreview.empty())
					{
						header.push_back(text("  ") | dim);
						header.push_back(text(argsPreview) | dim | color(Color::GrayLight));
					}

					Elements cardElems;
					cardElems.push_back(hbox(std::move(header)));

					// Body: output preview if completed (or expanded)
					bool showBody = (tc->status != "running" && !tc->output.empty()) || tc->expanded;
					if (showBody)
					{
						auto body = MarkdownRenderer::Render(tc->output);
						if (!tc->expanded)
						{
							// Cap to ~10 lines when collapsed: just render without cap for now
							// (FTXUI vbox doesn't easily truncate; future improvement)
						}
						cardElems.push_back(text(""));
						cardElems.push_back(body | color(Color::GrayLight));
					}

					children.push_back(vbox(std::move(cardElems)));
					break;
				}
			}
		}

		if (state->streaming && state->currentAssistantBuffer.empty())
		{
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

	auto input = Input(&inputContent, opt);

	auto withEnter = CatchEvent(input, [this](Event event) -> bool {
		if (event == Event::Return)
		{
			if (!inputContent.empty())
			{
				std::string text = inputContent;
				inputContent.clear();
				history.Add(text);
				historyCursor = history.Entries().size();
				SubmitPrompt(text);
			}
			return true;
		}
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
	using namespace ftxui;

	auto render = Renderer([this] {
		std::lock_guard lk(state->mutex);

		auto modelStr = state->model.empty() ? "no model" : state->model;
		auto modeStr = state->permissionMode == config::PermissionMode::Yolo
						   ? "YOLO"
						   : "Manual";

		// Status indicator (left side): spinner when streaming, dot when idle
		Element statusIndicator;
		if (state->streaming)
		{
			statusIndicator = hbox({
				spinner(7, spinnerFrame) | color(Color::Cyan),
				text(" working") | color(Color::Cyan),
			});
		}
		else
		{
			statusIndicator = text("* ready") | color(Color::Green);
		}

		Elements left;
		left.push_back(text(" "));
		left.push_back(text(modelStr) | bold | color(Color::Blue));
		left.push_back(text(" "));
		left.push_back(text(fmt::format("[{}]", modeStr)) | dim | color(Color::GrayLight));

		Elements right;
		int64_t tokens = TotalTokens(state->lastUsage);
		if (tokens > 0)
		{
			right.push_back(text(fmt::format(" {} tok", tokens)) | dim | color(Color::GrayLight));
			right.push_back(text("  "));
		}
		right.push_back(statusIndicator);
		right.push_back(text(" "));

		return vbox({
			separatorLight(),
			hbox({
				hbox(std::move(left)) | flex,
				hbox(std::move(right)),
			}),
		});
	});

	return render;
}

ftxui::Component TuiApp::MakeSidePanel()
{
	using namespace ftxui;
	return Renderer([] {
		return text("");
	});
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
		PostRender();
	}, allowStyle);

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
		PostRender();
	}, denyStyle);

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
			   }) | borderDouble | color(Color::Yellow) | size(WIDTH, EQUAL, 70) | center;

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
			   }) | border | color(Color::Cyan) | size(WIDTH, EQUAL, 70) | center;

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
				   text("    Escape      Close dialog"),
				   separator(),
				   text("  Slash commands:") | bold,
				   text("    /help       Show this help"),
				   text("    /clear      Clear context"),
				   text("    /exit       Quit"),
				   text("    /model      Pick model from list"),
				   text("    /model X    Switch to model X"),
				   text("    /sessions   Pick session to resume"),
				   text("    /mode       Toggle YOLO/Manual"),
				   separator(),
				   text("  Press Escape to close") | dim,
			   }) | border | center;
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
				if (title.size() > 50) title = title.substr(0, 47) + "...";
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

// ═══════════════════════════════════════════════════════════════════════
// Input handling
// ═══════════════════════════════════════════════════════════════════════

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

	return false;
}

bool TuiApp::HandleSlashCommand(std::string_view cmd)
{
	if (cmd == "/help" || cmd == "/?")
	{
		std::lock_guard lk(state->mutex);
		state->activeModal = ModalKind::Help;
		PostRender();
		return true;
	}
	if (cmd == "/clear")
	{
		(void)api->ClearContext(state->sessionId);
		std::lock_guard lk(state->mutex);
		state->transcript.clear();
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = "Context cleared.",
		});
		PostRender();
		return true;
	}
	if (cmd == "/exit" || cmd == "/quit")
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
	if (cmd == "/sessions" || cmd == "/session")
	{
		// Load sessions in background, then open the picker
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
	if (cmd == "/model")
	{
		// Load models in background, then open the settings dialog
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
	if (cmd.rfind("/model ", 0) == 0)
	{
		// /model <name> - set directly
		auto target = std::string(cmd.substr(7));
		auto status = api->SetModel(state->sessionId, target);
		if (status.ok())
		{
			std::lock_guard lk(state->mutex);
			state->model = target;
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = fmt::format("Model switched to: {}", target),
			});
		}
		else
		{
			std::lock_guard lk(state->mutex);
			state->transcript.push_back({
				.kind = TranscriptEntry::Kind::System,
				.text = fmt::format("Error: {}", status.message()),
			});
		}
		PostRender();
		return true;
	}
	if (cmd == "/mode" || cmd == "/yolo")
	{
		// Toggle permission mode
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
				.text = fmt::format("Mode: {}", newMode == config::PermissionMode::Yolo ? "YOLO" : "Manual"),
			});
		}
		PostRender();
		return true;
	}
	return false;
}

void TuiApp::SubmitPrompt(std::string text)
{
	if (text.empty() || state->sessionId.empty())
	{
		return;
	}

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

	// Detach a thread to run the prompt without blocking the UI
	std::thread([this, text] {
		auto result = api->Prompt(state->sessionId, text);
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
			{
				std::lock_guard lk(state->mutex);
				state->lastUsage = result->usage;
				state->streaming = false;
			}
			PostRender();
		}
	}).detach();
}

// ═══════════════════════════════════════════════════════════════════════
// Free: Top-level Run function
// ═══════════════════════════════════════════════════════════════════════

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
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include "Cli/CliOptions.h"
#include "Config/ConfigTypes.h"
#include "Permission/PermissionTypes.h"
#include "Rpc/CoreApi.h"
#include "Rpc/RpcTypes.h"
#include "Tui/EventRouter.h"
#include "Tui/TuiState.h"
#include "Tui/Utils/InputComposerLogic.h"
#include "Tui/Utils/InputHistory.h"

namespace codeharness::host
{
	class Host;
}
namespace codeharness::llm
{
	class HttpClient;
}

namespace codeharness::tui
{

	/// Top-level TUI application orchestrator.
	///
	/// Owns the FTXUI screen, component tree, CoreApi, and manages the
	/// event flow between the worker thread (Agent) and the UI thread.
	///
	/// Analogous to KimiTUI class in the Kimi Code TypeScript codebase.
	class TuiApp
	{
	public:
		/// Construct the TUI app with a CoreApi and parsed CLI options.
		TuiApp(std::unique_ptr<rpc::CoreApi> api, const cli::CliOptions& opts);
		~TuiApp();

		TuiApp(const TuiApp&) = delete;
		TuiApp& operator=(const TuiApp&) = delete;

		/// Enter the TUI event loop. Blocks until the user exits.
		absl::Status Run();

		/// Request a re-render (thread-safe, called from worker thread).
		void PostRender();

	private:
		// Lifecycle

		absl::Status InitializeSession();
		absl::Status Shutdown();

		// CoreApi callbacks (called from worker thread)

		void OnCoreEvent(const rpc::CoreEvent& event);
		permission::PermissionDecision OnApproval(std::string_view toolName,
												  const nlohmann::json& args,
												  std::string_view description);
		std::string OnQuestion(const tools::QuestionRequest& request);

		// FTXUI component construction

		ftxui::Component MakeLayout();
		ftxui::Component MakeMainContainer();
		ftxui::Component MakeChatPane();
		ftxui::Component MakeInputField();
		ftxui::Component MakeStatusBar();
		ftxui::Component MakeModalOverlay();
		ftxui::Component MakeApprovalPanel();
		ftxui::Component MakeQuestionDialog();
		ftxui::Component MakeHelpDialog();
		ftxui::Component MakeModelPicker();
		ftxui::Component MakeSessionPicker();
		ftxui::Component MakeSettingsDialog();
		ftxui::Component MakeSettingsModelDialog();
		ftxui::Component MakeSettingsPermissionDialog();
		ftxui::Component MakeSettingsThemeDialog();
		ftxui::Component MakeSettingsEditorDialog();
		ftxui::Component MakeSettingsUsageDialog();

		// Input handlers

		bool HandleInput(ftxui::Event event);
		bool HandleSlashCommand(std::string_view cmd);
		void SubmitPrompt(std::string text);

		// Data

		std::unique_ptr<rpc::CoreApi> api;
		std::shared_ptr<TuiState> state;
		std::string inputContent;					// shared with FTXUI Input component
		ftxui::ScreenInteractive* screen = nullptr; // borrowed; owned by Run()
		cli::CliOptions opts;

		// Event router (stateless, just mutates state)
		EventRouter router;

		// Modal tab index used by Container::Tab
		int activeModalIndex = 0;

		// Spinner animation frame counter (incremented each render)
		size_t spinnerFrame = 0;

		// Input history navigation state
		InputHistory history;
		HistoryNavigationState historyNav;

		// Atomic flag to signal re-render from worker thread
		std::atomic<bool> dirty{false};

		// Shutdown flag: once set, event callbacks are no-ops
		std::atomic<bool> stopped{false};
	};

	/// Top-level entry point: create CoreApi, wire callbacks, run the TUI.
	absl::Status Run(host::Host* host, llm::HttpClient* http, const cli::CliOptions& opts);

} // namespace codeharness::tui

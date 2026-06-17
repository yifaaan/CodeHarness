#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Config/ConfigTypes.h"
#include "Llm/Types.h"
#include "Permission/PermissionTypes.h"
#include "Session/SessionTypes.h"
#include "Tools/AskUser.h"

namespace codeharness::tui
{

	// ─── Forward declarations ───────────────────────────────────────────────

	struct ToolCallState;

	// ─── Todo item (rendered by TodoPanel; populated by TaskCreate tool events) ─

	struct TodoItem
	{
		enum class Status
		{
			Pending,
			InProgress,
			Done,
		};
		Status status = Status::Pending;
		std::string text;
	};

	// ─── Queued tool call (rendered by QueuePanel; populated by ToolScheduler) ─

	struct QueuedToolCall
	{
		std::string name;
		std::string preview; // short text preview of args
	};

	// ─── Message types rendered in the transcript ───────────────────────────

	/// One entry in the transcript (user, assistant text, tool call card, etc.)
	struct TranscriptEntry
	{
		enum class Kind
		{
			User,
			Assistant,
			ToolCall,
			System, // compaction notices, errors, status messages
		};

		Kind kind = Kind::Assistant;

		// For User / System messages
		std::string text;

		// For Assistant messages (streaming or completed)
		std::string assistantText;

		// For ToolCall messages — index into activeToolCalls or completedToolCalls
		std::string toolCallId;

		// Whether this entry is still being streamed (active assistant delta)
		bool streaming = false;

		// Whether this entry is a tool call that's still running
		bool toolRunning = false;
	};

	/// Runtime state of a tool call being tracked by the TUI
	struct ToolCallState
	{
		std::string id;
		std::string name;
		nlohmann::json args;
		std::string status = "running"; // "running" | "done" | "error"
		std::string output;
		std::string error;
		bool expanded = false; // user-toggled expand/collapse for output
	};

	/// Pending approval request awaiting user action
	struct PendingApproval
	{
		std::string toolName;
		nlohmann::json args;
		std::string description;
		std::shared_ptr<std::promise<permission::PermissionDecision>> promise;
	};

	/// Pending question awaiting user answer
	struct PendingQuestion
	{
		tools::QuestionRequest request;
		std::shared_ptr<std::promise<std::string>> promise;
	};

	/// Pending user text input (for when modal captures input)
	struct PendingInput
	{
		std::string placeholder;
		std::shared_ptr<std::promise<std::string>> promise;
	};

	// ─── Active modal kind ──────────────────────────────────────────────────

	enum class ModalKind
	{
		None,
		Approval,
		Question,
		Help,
		ModelPicker,
		SessionPicker,
		Settings,
	};

	// ─── Theme palette ──────────────────────────────────────────────────────

	struct ColorPalette
	{
		// Terminal colors
		int fg = 15;	   // white
		int bg = 0;		   // black
		int accent = 4;	   // blue
		int success = 2;   // green
		int error = 1;	   // red
		int warning = 3;   // yellow
		int muted = 8;	   // bright black (gray)
		int highlight = 6; // cyan
	};

	// ─── TuiState (mutex-protected, shared between UI thread and event thread) ─

	struct TuiState
	{
		// Session info
		std::string sessionId;
		std::string model;
		std::string workdir;
		std::string version = "CodeHarness v0.1.0";
		config::PermissionMode permissionMode = config::PermissionMode::Manual;

		// Available sessions (loaded by SessionPicker)
		std::vector<session::SessionInfo> availableSessions;
		std::vector<std::string> availableModels;
		bool sessionPickerAllScope = false;

		// Transcript
		std::deque<TranscriptEntry> transcript;
		std::string currentAssistantBuffer; // accumulating streaming text
		std::string currentThinking;		// extended-thinking stream (if any)

		// Tool tracking
		std::unordered_map<std::string, ToolCallState> activeToolCalls;
		std::unordered_map<std::string, ToolCallState> completedToolCalls;
		std::vector<QueuedToolCall> pendingToolCalls; // awaiting ToolScheduler
		int toolCallCount = 0;

		// Todo list (drives TodoPanel). Empty by default until TaskCreate fires.
		std::vector<TodoItem> todos;

		// Streaming state
		bool streaming = false;
		bool compacting = false;
		int compactingCount = 0;
		std::string currentActivity; // human-readable one-liner for ActivityIndicator

		// Token usage
		llm::TokenUsage lastUsage;
		std::string lastError;

		// Modal
		ModalKind activeModal = ModalKind::None;
		std::optional<PendingApproval> pendingApproval;
		std::optional<PendingQuestion> pendingQuestion;

		// Theme
		ColorPalette colors;
		bool darkMode = true;

		// Navigation / UI
		int scrollOffset = 0;
		int visibleHeight = 0;
		bool userScrolledUp = false; // suppress auto-scroll-to-bottom while user browses history
		std::string statusMessage;
		agent::AgentStatus agentStatus = agent::AgentStatus::Idle;
		bool toolOutputExpanded = false; // global Ctrl+O state for tool/thinking detail blocks

		// Visibility toggles (driven by keyboard shortcuts like Ctrl+B)
		bool sidePanelVisible = false;
		bool todoPanelVisible = true; // shown by default; can be toggled off

		// Mutex for thread-safe access
		mutable std::mutex mutex;
	};

	// ─── Free functions ─────────────────────────────────────────────────────

	/// Determine whether the terminal is using a dark or light background.
	bool DetectDarkMode();

	/// Build a default ColorPalette for the given mode.
	ColorPalette MakePalette(bool darkMode);

	/// Apply the global tool-output expansion state to all tracked tool cards.
	void ApplyToolOutputExpanded(TuiState& state, bool expanded);

} // namespace codeharness::tui

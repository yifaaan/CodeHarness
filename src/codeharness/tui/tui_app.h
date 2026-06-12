#pragma once

#include "codeharness/core/error.h"
#include "codeharness/engine/engine.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/runtime/runtime.h"
#include "codeharness/tui/bottom_pane/bottom_pane.h"
#include "codeharness/tui/chat_surface.h"
#include "codeharness/tui/tui_event.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::tui
{

struct TokenUsage
{
    int input_tokens = 0;
    int output_tokens = 0;
};

struct McpConnectionInfo
{
    int connected = 0;
    int failed = 0;
};

struct TuiDisplayConfig
{
    std::string product_name = "CodeHarness";
    std::string model = "unknown";
    std::string provider_type = "unknown";
    std::string version = "0.1.0";
    std::string directory;
    std::string context_left_label = "100% context left";
    std::string startup_tip = "New Build faster with CodeHarness.";
    std::vector<std::string> status_line_items;
    int skill_count = 0;
    TokenUsage token_usage;
    McpConnectionInfo mcp_info;
};

struct TuiAppEventContext
{
    PermissionMode permission_mode = PermissionMode::Default;
    std::optional<SessionCommandSummary> active_session;
};

struct TuiAppEventResult
{
    bool refresh_requested = false;
    bool clear_permission_response = false;
    bool clear_user_question_response = false;
    bool release_cancellation = false;
    bool run_completed = false;
    std::optional<TokenUsage> token_usage;
};

enum class TuiAction
{
    None,
    SubmitPrompt,
    InsertCommand,
    SelectModel,
    SubmitQuestion,
    ApprovePermission,
    ApprovePermissionForSession,
    DenyPermission,
    Interrupt,
    Quit,
};

enum class TuiInputKind
{
    unknown,
    arrow_up,
    arrow_down,
    backspace,
    escape,
    submit,
    newline,
    interrupt,
    character,
};

struct TuiInput
{
    TuiInputKind kind = TuiInputKind::unknown;
    char character = '\0';
};

struct TuiInterruptResult
{
    bool interrupted = false;
    std::optional<PermissionResponse> permission_response;
    bool cancel_user_question = false;
};

struct TuiFocusedInputResult
{
    bool handled = false;
    TuiAction action = TuiAction::None;
    bool composer_changed = false;
    std::optional<PermissionResponse> permission_response;
    std::optional<UserQuestionResponse> user_question_response;
    std::optional<ModelOption> selected_model;
    TuiInterruptResult interrupt;
};

struct TuiComposerSubmitResult
{
    bool handled = false;
    TuiAction action = TuiAction::None;
    bool request_model_selector = false;
    bool composer_changed = false;
    std::string prompt;
};

struct TuiComposerPasteResult
{
    bool handled = false;
    bool composer_changed = false;
};

struct TuiState
{
    std::vector<TranscriptItem> transcript;
    std::size_t transcript_revision = 0;
    bool follow_transcript = true;
    std::string composer;
    std::optional<SessionCommandSummary> active_session;
    bool busy = false;
    bool interrupt_requested = false;
    bool should_quit = false;
    PermissionMode permission_mode = PermissionMode::Default;
    BottomPaneFocus bottom_pane_focus = BottomPaneFocus::composer;
    std::optional<PermissionPrompt> pending_permission;
    std::optional<CommandPaletteState> command_palette;
    std::optional<SelectModalState> select_modal;
    std::optional<QuestionModalState> question_modal;
    bool paste_burst_active = false;
};

struct CodexFrameState
{
    TuiDisplayConfig display;
    TuiState state;
    int width = 80;
    int height = 24;
    int elapsed_seconds = 0;
    int composer_history_index = -1;
};

class TuiAppModel
{
public:
    [[nodiscard]] auto state() const noexcept -> const TuiState&;
    [[nodiscard]] auto render_text(int width = 80) const -> std::string;

    auto set_composer(std::string value) -> void;
    auto open_command_palette(std::vector<CommandPaletteEntry> commands) -> void;
    auto close_command_palette() -> void;
    auto update_command_palette_from_composer() -> void;
    auto command_palette_input(char character) -> void;
    auto command_palette_backspace() -> void;
    auto command_palette_up() -> void;
    auto command_palette_down() -> void;
    auto selected_command_text() const -> std::optional<std::string>;
    auto handle_submit() -> TuiAction;
    [[nodiscard]] auto handle_composer_submit(std::string content) -> TuiComposerSubmitResult;
    [[nodiscard]] auto handle_composer_slash_start(std::vector<CommandPaletteEntry> commands) -> bool;
    auto handle_quit() -> TuiAction;
    auto handle_command_select() -> TuiAction;
    auto handle_command_cancel() -> TuiAction;
    auto handle_interrupt() -> TuiAction;
    [[nodiscard]] auto request_interrupt() -> TuiInterruptResult;
    auto handle_permission_approve() -> TuiAction;
    auto handle_permission_approve_for_session() -> TuiAction;
    auto handle_permission_deny() -> TuiAction;
    [[nodiscard]] auto toggle_tool_details(std::size_t transcript_index) -> bool;

    // Select modal (/model picker)
    auto open_select_modal(std::string title, std::vector<ModelOption> options) -> void;
    auto close_select_modal() -> void;
    auto select_modal_up() -> void;
    auto select_modal_down() -> void;
    auto select_modal_input(char character) -> void;
    auto select_modal_backspace() -> void;
    auto handle_select_cancel() -> TuiAction;
    [[nodiscard]] auto select_modal_current() const -> std::optional<ModelOption>;
    [[nodiscard]] auto select_modal_quick_select(int digit) -> std::optional<ModelOption>;

    // Question modal (AskUser)
    auto show_question(std::string request_id, std::string question, std::string tool_name, std::string reason) -> void;
    auto close_question() -> void;
    auto question_modal_input(char character) -> void;
    auto question_modal_backspace() -> void;
    auto question_modal_newline() -> void;
    [[nodiscard]] auto question_modal_submit() -> std::string;
    [[nodiscard]] auto handle_focused_bottom_pane_input(const TuiInput& input) -> TuiFocusedInputResult;
    auto set_active_session(std::optional<SessionCommandSummary> summary) -> void;

    // Paste burst detection
    auto detect_paste_burst(const std::string& input) -> void;
    auto apply_paste_to_composer(const std::string& paste_text) -> void;
    [[nodiscard]] auto handle_composer_paste(std::string paste_text) -> TuiComposerPasteResult;
    [[nodiscard]] auto handle_transcript_wheel(bool wheel_up, bool wheel_down) -> bool;

    auto begin_prompt(std::string prompt) -> void;
    auto complete_prompt() -> void;
    auto apply_engine_event(const EngineEvent& event) -> void;
    [[nodiscard]] auto has_active_response() const noexcept -> bool;
    [[nodiscard]] auto apply_tui_event(const TuiEvent& event, const TuiAppEventContext& context = {})
        -> TuiAppEventResult;
    [[nodiscard]] auto has_streamed_assistant_output() const noexcept -> bool;
    auto show_permission(const PermissionPrompt& prompt) -> void;
    auto clear_permission() -> void;
    auto set_permission_mode(PermissionMode mode) -> void
    {
        state_.permission_mode = mode;
    }
    auto append_system_message(std::string text) -> void;

private:
    auto sync_transcript_view() -> void;
    auto sync_bottom_pane_view() -> void;

    TuiState state_;
    ChatSurface chat_;
    BottomPane bottom_pane_;
};

using ModelListProvider = std::function<std::vector<ModelOption>()>;
using ModelSelectCallback = std::function<absl::StatusOr<ModelOption>(const ModelOption&)>;

auto apply_transcript_follow_wheel(bool current_follow, bool wheel_up, bool wheel_down) -> bool;

auto run_tui(runtime::RuntimeBundle& runtime,
             int max_turns,
             TuiDisplayConfig display_config = {},
             ModelListProvider model_list_provider = {},
             ModelSelectCallback model_select_callback = {}) -> absl::StatusOr<int>;

} // namespace codeharness::tui

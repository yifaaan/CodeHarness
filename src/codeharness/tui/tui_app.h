#pragma once

#include "codeharness/core/result.h"
#include "codeharness/engine/engine.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/runtime/runtime.h"

#include <optional>
#include <string>
#include <vector>

namespace codeharness::tui
{

enum class TuiAction
{
    None,
    SubmitPrompt,
    InsertCommand,
    ApprovePermission,
    DenyPermission,
    Interrupt,
    Quit,
};

struct TranscriptItem
{
    std::string kind;
    std::string text;
    bool is_error = false;
};

struct CommandPaletteEntry
{
    std::string name;
    std::string description;
    std::vector<std::string> aliases;
};

struct CommandPaletteState
{
    std::vector<CommandPaletteEntry> commands;
    std::vector<std::size_t> matches;
    std::string query;
    std::size_t cursor = 0;
};

struct TuiState
{
    std::vector<TranscriptItem> transcript;
    std::string composer;
    bool busy = false;
    bool interrupt_requested = false;
    bool should_quit = false;
    PermissionMode permission_mode = PermissionMode::Default;
    std::optional<PermissionPrompt> pending_permission;
    std::optional<CommandPaletteState> command_palette;
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
    auto handle_quit() -> TuiAction;
    auto handle_command_select() -> TuiAction;
    auto handle_command_cancel() -> TuiAction;
    auto handle_interrupt() -> TuiAction;
    auto handle_permission_approve() -> TuiAction;
    auto handle_permission_deny() -> TuiAction;

    auto begin_prompt(std::string prompt) -> void;
    auto complete_prompt() -> void;
    auto apply_engine_event(const EngineEvent& event) -> void;
    auto show_permission(const PermissionPrompt& prompt) -> void;
    auto clear_permission() -> void;
    auto set_permission_mode(PermissionMode mode) -> void { state_.permission_mode = mode; }

private:
    auto refresh_command_palette_matches() -> void;

    TuiState state_;
};

auto run_tui(runtime::RuntimeBundle& runtime, int max_turns) -> Result<int>;

} // namespace codeharness::tui

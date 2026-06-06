#pragma once

#include "codeharness/core/result.h"
#include "codeharness/engine/engine.h"
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
    ApprovePermission,
    DenyPermission,
    Quit,
};

struct TranscriptItem
{
    std::string kind;
    std::string text;
    bool is_error = false;
};

struct TuiState
{
    std::vector<TranscriptItem> transcript;
    std::string composer;
    bool busy = false;
    bool should_quit = false;
    std::optional<PermissionPrompt> pending_permission;
};

class TuiAppModel
{
public:
    [[nodiscard]] auto state() const noexcept -> const TuiState&;
    [[nodiscard]] auto render_text(int width = 80) const -> std::string;

    auto set_composer(std::string value) -> void;
    auto handle_submit() -> TuiAction;
    auto handle_quit() -> TuiAction;
    auto handle_permission_approve() -> TuiAction;
    auto handle_permission_deny() -> TuiAction;

    auto begin_prompt(std::string prompt) -> void;
    auto complete_prompt() -> void;
    auto apply_engine_event(const EngineEvent& event) -> void;
    auto show_permission(const PermissionPrompt& prompt) -> void;
    auto clear_permission() -> void;

private:
    TuiState state_;
};

auto run_tui(runtime::RuntimeBundle& runtime, int max_turns) -> Result<int>;

} // namespace codeharness::tui

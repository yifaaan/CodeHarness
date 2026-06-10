#pragma once

#include "codeharness/engine/engine.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::tui
{

struct ModelOption
{
    std::string value;        // profile name
    std::string label;        // display name
    std::string description;  // provider type + base_url summary
    bool is_current = false;
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

struct SelectModalState
{
    std::string title;
    std::vector<ModelOption> options;
    std::string query;
    std::vector<std::size_t> matches;
    std::size_t cursor = 0;
    bool is_searchable = false;
};

struct QuestionModalState
{
    std::string request_id;
    std::string question;
    std::string tool_name;
    std::string reason;
    std::string answer;
    std::vector<std::string> extra_lines;
};

struct BottomPaneState
{
    std::string composer;
    std::optional<PermissionPrompt> pending_permission;
    std::optional<CommandPaletteState> command_palette;
    std::optional<SelectModalState> select_modal;
    std::optional<QuestionModalState> question_modal;
    bool paste_burst_active = false;
};

class BottomPane
{
public:
    [[nodiscard]] auto state() const noexcept -> const BottomPaneState&;

    auto set_composer(std::string value) -> void;
    auto clear_prompt_entry() -> void;
    [[nodiscard]] auto can_submit_prompt() const -> bool;
    [[nodiscard]] auto can_quit() const -> bool;
    [[nodiscard]] auto has_pending_permission() const -> bool;
    auto clear_for_interrupt() -> void;

    auto open_command_palette(std::vector<CommandPaletteEntry> commands, bool busy) -> void;
    auto close_command_palette() -> void;
    auto update_command_palette_from_composer() -> void;
    auto command_palette_input(char character, bool busy) -> void;
    auto command_palette_backspace(bool busy) -> void;
    auto command_palette_up() -> void;
    auto command_palette_down() -> void;
    [[nodiscard]] auto selected_command_text() const -> std::optional<std::string>;
    [[nodiscard]] auto handle_command_select() -> bool;
    auto handle_command_cancel() -> void;

    auto show_permission(const PermissionPrompt& prompt) -> void;
    auto clear_permission() -> void;
    [[nodiscard]] auto handle_permission_approve() -> bool;
    [[nodiscard]] auto handle_permission_approve_for_session() -> bool;
    [[nodiscard]] auto handle_permission_deny() -> bool;

    auto open_select_modal(std::string title, std::vector<ModelOption> options, bool busy) -> void;
    auto close_select_modal() -> void;
    auto select_modal_up() -> void;
    auto select_modal_down() -> void;
    auto select_modal_input(char character, bool busy) -> void;
    auto select_modal_backspace(bool busy) -> void;
    auto handle_select_cancel() -> void;
    [[nodiscard]] auto select_modal_current() const -> std::optional<ModelOption>;
    [[nodiscard]] auto select_modal_quick_select(int digit) const -> std::optional<ModelOption>;

    auto show_question(std::string request_id, std::string question, std::string tool_name, std::string reason) -> void;
    auto close_question() -> void;
    auto question_modal_input(char character) -> void;
    auto question_modal_backspace() -> void;
    auto question_modal_newline() -> void;
    [[nodiscard]] auto question_modal_submit() const -> std::string;

    auto detect_paste_burst(const std::string& input) -> void;
    auto apply_paste_to_composer(const std::string& paste_text, bool busy) -> void;

private:
    auto refresh_command_palette_matches() -> void;
    auto refresh_select_modal_matches() -> void;

    BottomPaneState state_;
};

} // namespace codeharness::tui

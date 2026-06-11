#include "codeharness/tui/tui_render.h"

#include "codeharness/tui/list_dialog_render.h"
#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <sstream>
#include <string>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

auto card_width_for(const TuiDisplayConfig& config) -> int
{
    const auto model_line = std::string{"model: "} + config.model + "  /model to change";
    const auto directory_line = std::string{"directory: "} + config.directory;
    const auto title_line = std::string{">_ "} + config.product_name + " (v" + config.version + ")";
    return std::max({36,
                     static_cast<int>(title_line.size()),
                     static_cast<int>(model_line.size()),
                     static_cast<int>(directory_line.size())}) + 4;
}

auto pad_to(std::string text, int width) -> std::string
{
    if (static_cast<int>(text.size()) < width)
    {
        text.append(static_cast<std::size_t>(width - static_cast<int>(text.size())), ' ');
    }
    return text;
}

auto command_palette_spec(const CommandPaletteState& palette) -> ListDialogSpec
{
    ListDialogSpec spec{
        .title = "Select a command",
        .query = palette.query,
        .is_searchable = true,
        .cursor = palette.cursor,
        .page_size = static_cast<std::size_t>(k_command_palette_page_size),
    };
    spec.rows.reserve(palette.matches.size());
    for (const auto command_index : palette.matches)
    {
        const auto& command = palette.commands.at(command_index);
        spec.rows.push_back(ListDialogRow{
            .primary = "/" + command.name,
            .secondary = command.description,
        });
    }
    return spec;
}

auto select_modal_spec(const SelectModalState& modal) -> ListDialogSpec
{
    ListDialogSpec spec{
        .title = modal.title,
        .query = modal.query,
        .is_searchable = modal.is_searchable,
        .cursor = modal.cursor,
        .page_size = static_cast<std::size_t>(k_command_palette_page_size),
    };
    spec.rows.reserve(modal.matches.size());
    for (const auto option_index : modal.matches)
    {
        const auto& option = modal.options.at(option_index);
        spec.rows.push_back(ListDialogRow{
            .primary = option.label,
            .secondary = option.description,
            .is_current = option.is_current,
        });
    }
    return spec;
}

auto transcript_view_lines(const std::vector<TranscriptItem>& transcript,
                           int width,
                           int height,
                           bool follow_transcript) -> std::vector<std::string>
{
    auto lines = render_transcript_lines(transcript, width);
    if (height <= 0 || static_cast<std::size_t>(height) >= lines.size())
    {
        return lines;
    }

    if (follow_transcript)
    {
        const auto first = lines.end() - height;
        return std::vector<std::string>{first, lines.end()};
    }

    const auto last = lines.begin() + height;
    return std::vector<std::string>{lines.begin(), last};
}

auto composer_frame_lines(const CodexFrameState& frame) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(k_codex_composer_rows));

    auto content = frame.state.composer;
    if (content.empty())
    {
        content = "Ask " + frame.display.product_name + " to do anything";
    }

    std::istringstream stream{content};
    std::string line;
    for (int index = 0; index < k_codex_composer_rows; ++index)
    {
        if (std::getline(stream, line))
        {
            lines.push_back((index == 0 ? std::string{k_codex_prompt_prefix} : "  ") + line);
        }
        else
        {
            lines.push_back("  ");
        }
    }
    return lines;
}

auto transcript_line_element(const std::string& line) -> Element
{
    if (line.empty())
    {
        return text(" ") | user_message_bg_style();
    }

    if (line.starts_with(std::string{k_codex_prompt_prefix}))
    {
        const auto content = line.substr(std::string{k_codex_prompt_prefix}.size());
        return hbox({
            text(std::string{k_codex_prompt_prefix}) | color(TuiTheme::codex_user_prefix()) | bold | dim,
            text(content) | color(TuiTheme::text_strong()),
        }) | user_message_bg_style();
    }

    const auto tree_prefix = std::string{"  \xe2\x94\x82 "};
    const auto last_prefix = std::string{"  \xe2\x94\x94 "};
    if (line.starts_with(tree_prefix) || line.starts_with(last_prefix))
    {
        const auto prefix = line.starts_with(tree_prefix) ? tree_prefix : last_prefix;
        return hbox({
            text(prefix) | color(TuiTheme::codex_output_dim()),
            text(line.substr(prefix.size())) | color(TuiTheme::codex_output_dim()) | dim,
        });
    }

    if (line.starts_with("  \xe2\x80\xa6 "))
    {
        return text(line) | color(TuiTheme::codex_output_dim()) | dim;
    }

    if (line.starts_with(std::string{k_codex_bullet}))
    {
        const auto content = line.substr(std::string{k_codex_bullet}.size());
        if (!content.starts_with("Running ") && !content.starts_with("Ran ") && !content.starts_with("Failed "))
        {
            return hbox({
                text(std::string{k_codex_bullet}) | color(TuiTheme::codex_bullet()) | dim,
                text(content) | color(TuiTheme::text_default()),
            });
        }
        return hbox({
            text(std::string{k_codex_bullet}) | color(TuiTheme::codex_bullet()) | dim,
            styled_line_element(codex_command_header_segments(content)),
        });
    }

    if (line.starts_with("  \xe2\x9c\x95 "))
    {
        return text(line) | color(TuiTheme::error()) | bold;
    }

    return text(line) | color(TuiTheme::text_default());
}

} // namespace

auto bottom_pane_reserved_rows(BottomPaneFocus focus) -> int
{
    if (bottom_pane_accepts_composer_input(focus))
    {
        return k_codex_status_rows + k_codex_composer_rows + k_codex_hint_rows + k_codex_footer_rows;
    }
    return 2;
}

auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    const auto width = card_width_for(config);
    const auto inner_width = width - 2;

    lines.push_back(std::string{k_box_corner_tl} + horizontal_rule(inner_width) + std::string{k_box_corner_tr});
    lines.push_back(std::string{k_box_vertical} + " " +
                    pad_to(">_ " + config.product_name + " (v" + config.version + ")", inner_width - 1) +
                    std::string{k_box_vertical});
    lines.push_back(std::string{k_box_vertical} + pad_to("", inner_width) + std::string{k_box_vertical});
    lines.push_back(std::string{k_box_vertical} + " " +
                    pad_to("model:      " + config.model + "  /model to change", inner_width - 1) +
                    std::string{k_box_vertical});
    lines.push_back(std::string{k_box_vertical} + " " +
                    pad_to("directory:  " + config.directory, inner_width - 1) +
                    std::string{k_box_vertical});
    lines.push_back(std::string{k_box_corner_bl} + horizontal_rule(inner_width) + std::string{k_box_corner_br});
    lines.push_back("");
    if (!config.startup_tip.empty())
    {
        lines.push_back("Tip: " + config.startup_tip);
        lines.push_back("");
    }
    return lines;
}

auto render_codex_frame_lines(const CodexFrameState& frame) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    if (frame.state.transcript.empty())
    {
        lines = render_welcome_lines(frame.display);
    }
    else
    {
        lines = render_transcript_lines(frame.state.transcript, frame.width);
    }

    if (bottom_pane_accepts_composer_input(frame.state.bottom_pane_focus))
    {
        if (should_animate_working_status(frame.state))
        {
            lines.push_back(std::string{k_codex_bullet} + "Working (" + std::to_string(frame.elapsed_seconds) +
                            "s \xe2\x80\xa2 esc to interrupt)");
        }
        else if (frame.state.interrupt_requested)
        {
            lines.push_back(std::string{k_codex_bullet} + "Interrupting...");
        }
        else
        {
            lines.push_back("");
        }

        auto composer_lines = composer_frame_lines(frame);
        lines.insert(lines.end(), std::make_move_iterator(composer_lines.begin()), std::make_move_iterator(composer_lines.end()));
        const auto hint = frame.state.composer.empty()
                              ? "Use /skills to list available skills"
                              : render_composer_hint(frame.state.busy, frame.composer_history_index);
        lines.push_back("  " + hint);
    }
    else
    {
        lines.push_back("");
    }
    lines.push_back("  " + render_status_footer_line(frame.display, frame.state));
    return lines;
}

auto render_transcript_lines(const std::vector<TranscriptItem>& transcript, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    for (const auto& item : transcript)
    {
        auto item_lines = render_history_cell_lines(item, width);
        lines.insert(lines.end(), std::make_move_iterator(item_lines.begin()), std::make_move_iterator(item_lines.end()));
    }
    return lines;
}

auto render_command_palette_lines(const CommandPaletteState& palette, int width) -> std::vector<std::string>
{
    return render_list_dialog_lines(command_palette_spec(palette), width);
}

auto render_select_modal_lines(const SelectModalState& modal, int width) -> std::vector<std::string>
{
    return render_list_dialog_lines(select_modal_spec(modal), width);
}

auto render_permission_lines(const PermissionPrompt& prompt, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back("\xe2\x94\x8c Allow " + prompt.tool_name + "?");
    if (!prompt.reason.empty())
    {
        lines.push_back("\xe2\x94\x82 " + trim_to_width(prompt.reason, width > 2 ? width - 2 : width));
    }
    if (prompt.command)
    {
        lines.push_back("\xe2\x94\x82 command: " + trim_to_width(*prompt.command, width > 11 ? width - 11 : width));
    }
    if (prompt.path)
    {
        lines.push_back("\xe2\x94\x82 path: " + trim_to_width(prompt.path->string(), width > 8 ? width - 8 : width));
    }
    lines.push_back("\xe2\x94\x94 y allow once \xc2\xb7 a allow session \xc2\xb7 n/d deny \xc2\xb7 Esc cancel");
    return lines;
}

auto render_question_lines(const QuestionModalState& modal, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back("Agent is waiting for your input...");
    lines.push_back("");
    lines.push_back("? " + trim_to_width(modal.question, width > 2 ? width - 2 : width));
    if (!modal.tool_name.empty())
    {
        lines.push_back("Tool: " + trim_to_width(modal.tool_name, width > 6 ? width - 6 : width));
    }
    if (!modal.reason.empty())
    {
        lines.push_back("Reason: " + trim_to_width(modal.reason, width > 8 ? width - 8 : width));
    }
    if (!modal.extra_lines.empty())
    {
        lines.push_back("");
        for (const auto& line : modal.extra_lines)
        {
            lines.push_back("  " + trim_to_width(line, width > 2 ? width - 2 : width));
        }
    }
    lines.push_back("");
    lines.push_back("> " + trim_to_width(modal.answer, width > 2 ? width - 2 : width));
    lines.push_back("Shift+Enter newline \xc2\xb7 Enter submit");
    return lines;
}

auto welcome_banner_element(const TuiDisplayConfig& config) -> Element
{
    using namespace ftxui;

    Elements rows;

    rows.push_back(hbox({
        text(">_ ") | dim,
        text(config.product_name) | bold,
        text(" (v" + config.version + ")") | dim,
    }));
    rows.push_back(text(" "));
    rows.push_back(hbox({
        text("model:     ") | dim,
        text(config.model),
        text("  "),
        text("/model") | color(TuiTheme::primary()) | bold,
        text(" to change") | dim,
    }));
    rows.push_back(hbox({
        text("directory: ") | dim,
        text(config.directory),
    }));

    auto card = vbox(std::move(rows)) | border | color(TuiTheme::border_default());
    Elements outer;
    outer.push_back(card);
    outer.push_back(text(" "));
    if (!config.startup_tip.empty())
    {
        outer.push_back(text("Tip: " + config.startup_tip));
    }

    return vbox(std::move(outer));
}

auto transcript_item_element(const TranscriptItem& item, int width) -> Element
{
    return history_cell_element(item, width);
}

auto transcript_view_element(const std::vector<TranscriptItem>& transcript,
                             int width,
                             int height,
                             bool follow_transcript) -> Element
{
    Elements rows;
    auto lines = transcript_view_lines(transcript, width, height, follow_transcript);
    rows.reserve(lines.size() + 2);

    // Codex: model header bar at top
    // (model info is added by tui_app.cpp in the layout, not here)

    if (follow_transcript)
    {
        rows.push_back(filler());
    }
    for (const auto& line : lines)
    {
        rows.push_back(transcript_line_element(line));
    }

    return vbox(std::move(rows)) | flex;
}

auto model_header_element(const TuiDisplayConfig& config) -> Element
{
    return status_footer_element(config, TuiState{});
}

auto command_palette_element(const CommandPaletteState& palette, int width) -> Element
{
    return list_dialog_element(command_palette_spec(palette), width);
}

auto permission_modal_element(const PermissionPrompt& prompt, int width) -> Element
{
    Elements rows;
    rows.push_back(hbox({
        text("\xe2\x94\x8c Allow ") | color(TuiTheme::warning()) | bold,
        text(prompt.tool_name) | color(TuiTheme::primary()) | bold,
        text("?") | bold,
    }));
    if (!prompt.reason.empty())
    {
        rows.push_back(hbox({text("\xe2\x94\x82 ") | color(TuiTheme::warning()), text(prompt.reason) | dim}));
    }
    if (prompt.command)
    {
        rows.push_back(hbox({text("\xe2\x94\x82 command: ") | color(TuiTheme::warning()) | dim, text(*prompt.command)}));
    }
    if (prompt.path)
    {
        rows.push_back(hbox({text("\xe2\x94\x82 path: ") | color(TuiTheme::warning()) | dim, text(prompt.path->string())}));
    }
    rows.push_back(hbox({
        text("\xe2\x94\x94 ") | color(TuiTheme::warning()),
        text("[y] Allow once") | color(TuiTheme::success()),
        text("  "),
        text("[a] Allow session") | color(TuiTheme::success()),
        text("  "),
        text("[n/d] Deny") | color(TuiTheme::error()),
        text("  "),
        text("Esc cancel") | dim,
    }));
    return vbox(std::move(rows));
}

auto select_modal_element(const SelectModalState& modal, int width) -> Element
{
    return list_dialog_element(select_modal_spec(modal), width);
}

auto question_modal_element(const QuestionModalState& modal, int width) -> Element
{
    Elements rows;

    rows.push_back(text("\xe2\x8f\xb3 Agent is waiting for your input...") | color(TuiTheme::warning()) | dim);

    rows.push_back(text(" "));
    rows.push_back(hbox({
        text("\xe2\x9d\x93 ") | color(TuiTheme::warning()) | bold,
        text(modal.question) | bold,
    }));

    if (!modal.tool_name.empty())
    {
        rows.push_back(hbox({
            text("  Tool: ") | dim,
            text(modal.tool_name) | color(TuiTheme::primary()),
        }));
    }
    if (!modal.reason.empty())
    {
        rows.push_back(hbox({text("  Reason: ") | dim, text(modal.reason) | dim}));
    }

    if (!modal.extra_lines.empty())
    {
        rows.push_back(text(" "));
        for (const auto& line : modal.extra_lines)
        {
            rows.push_back(text("  " + line) | dim);
        }
    }

    rows.push_back(text(" "));
    Element input_element;
    if (modal.answer.empty())
    {
        input_element = hbox({
            text("> ") | color(TuiTheme::primary()),
            text(" ") | inverted,
        });
    }
    else
    {
        input_element = hbox({
            text("> ") | color(TuiTheme::primary()),
            text(modal.answer.substr(0, modal.answer.size() - 1)),
            text(std::string(1, modal.answer.back())) | inverted,
        });
    }
    rows.push_back(input_element);

    rows.push_back(text("  Shift+Enter newline \xc2\xb7 Enter submit") | dim);

    return vbox(std::move(rows)) | borderDouble | color(TuiTheme::warning());
}

} // namespace codeharness::tui::render

#include "codeharness/tui/tui_render.h"

#include "codeharness/tui/list_dialog_render.h"
#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/style.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

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
        .title = modal.title == "Select model" ? std::string{"Select Model and Effort"} : modal.title,
        .query = modal.query,
        .is_searchable = modal.is_searchable,
        .cursor = modal.cursor,
        .page_size = static_cast<std::size_t>(k_command_palette_page_size),
        .layout = ListDialogLayout::model_picker,
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
        return k_codex_status_rows + k_codex_composer_rows + k_codex_footer_rows;
    }
    return 2;
}

auto composer_prompt_prefix_column_element() -> Element
{
    Elements rows;
    rows.reserve(static_cast<std::size_t>(k_codex_composer_rows));
    rows.push_back(text(std::string{k_codex_prompt_prefix}) | color(TuiTheme::codex_user_prefix()) | bold | dim);
    for (int index = 1; index < k_codex_composer_rows; ++index)
    {
        rows.push_back(text("  "));
    }
    return vbox(std::move(rows)) | size(HEIGHT, EQUAL, k_codex_composer_rows);
}

auto composer_input_area_element(Element child) -> Element
{
    return dbox({
        filler() | bgcolor(TuiTheme::user_message_bg()),
        std::move(child) | bgcolor(TuiTheme::user_message_bg()),
    }) | size(HEIGHT, EQUAL, k_codex_composer_rows);
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

        // Codex single-line footer: hint left, status right
        const auto hint = frame.state.composer.empty()
                              ? "Use /skills to list available skills"
                              : render_composer_hint(frame.state.busy, frame.composer_history_index);
        const auto status_text = render_status_footer_line(frame.display, frame.state);
        const auto hint_part = "  " + hint;
        const auto filler_needed = std::max(0, frame.width - 2 - static_cast<int>(hint.size()) - static_cast<int>(status_text.size()));
        lines.push_back(hint_part + std::string(static_cast<std::size_t>(filler_needed), ' ') + status_text);
    }
    else
    {
        lines.push_back("");
        lines.push_back("  " + render_status_footer_line(frame.display, frame.state));
    }
    return lines;
}

auto render_codex_screen(const CodexFrameState& frame) -> ftxui::Screen
{
    Elements rows;
    const auto transcript_height = std::max(1, frame.height - bottom_pane_reserved_rows(frame.state.bottom_pane_focus));
    if (frame.state.transcript.empty())
    {
        rows.push_back(welcome_banner_element(frame.display) | size(HEIGHT, EQUAL, transcript_height));
    }
    else
    {
        rows.push_back(transcript_view_element(frame.state.transcript,
                                               frame.width,
                                               transcript_height,
                                               frame.state.follow_transcript) |
                       size(HEIGHT, EQUAL, transcript_height));
    }

    if (bottom_pane_accepts_composer_input(frame.state.bottom_pane_focus))
    {
        if (should_animate_working_status(frame.state))
        {
            rows.push_back(working_status_element(frame.elapsed_seconds, "Working", frame.elapsed_seconds) |
                           size(HEIGHT, EQUAL, k_codex_status_rows));
        }
        else if (frame.state.interrupt_requested)
        {
            rows.push_back(hbox({
                               text(std::string{k_codex_bullet}) | color(TuiTheme::warning()),
                               text("Interrupting...") | color(TuiTheme::warning()) | bold,
                           }) |
                           size(HEIGHT, EQUAL, k_codex_status_rows));
        }
        else
        {
            rows.push_back(text(" ") | size(HEIGHT, EQUAL, k_codex_status_rows));
        }

        Elements composer_rows;
        auto composer_lines = composer_frame_lines(frame);
        for (auto& line : composer_lines)
        {
            if (line.starts_with(std::string{k_codex_prompt_prefix}))
            {
                const auto content = line.substr(std::string{k_codex_prompt_prefix}.size());
                auto content_element = text(content.empty() ? std::string{" "} : content);
                if (frame.state.composer.empty())
                {
                    content_element = content_element | dim;
                }
                else
                {
                    content_element = content_element | color(TuiTheme::text_default());
                }
                composer_rows.push_back(hbox({
                    text(std::string{k_codex_prompt_prefix}) | color(TuiTheme::codex_user_prefix()) | bold | dim,
                    std::move(content_element),
                }));
            }
            else
            {
                composer_rows.push_back(text(line.empty() ? " " : line));
            }
        }
        rows.push_back(composer_input_area_element(vbox(std::move(composer_rows))));
        const auto hint = frame.state.composer.empty()
                              ? "Use /skills to list available skills"
                              : render_composer_hint(frame.state.busy, frame.composer_history_index);
        rows.push_back(codex_footer_element(frame.display, frame.state, hint) |
                       size(HEIGHT, EQUAL, k_codex_footer_rows));
    }
    else
    {
        rows.push_back(status_footer_element(frame.display, frame.state) |
                       size(HEIGHT, EQUAL, k_codex_footer_rows));
    }

    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(frame.width), ftxui::Dimension::Fixed(frame.height));
    ftxui::Render(screen, vbox(std::move(rows)) | flex | codex_background_style());
    return screen;
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
    lines.push_back("");
    lines.push_back("  Would you like to allow " + prompt.tool_name + "?");
    lines.push_back("");
    if (!prompt.reason.empty())
    {
        lines.push_back("  Reason: " + trim_to_width(prompt.reason, width > 10 ? width - 10 : width));
    }

    std::vector<std::string> permission_parts;
    if (prompt.command && !prompt.command->empty())
    {
        permission_parts.push_back("command `" + *prompt.command + "`");
    }
    if (prompt.path)
    {
        permission_parts.push_back("path `" + prompt.path->string() + "`");
    }
    if (!permission_parts.empty())
    {
        std::string rule = permission_parts.front();
        for (std::size_t index = 1; index < permission_parts.size(); ++index)
        {
            rule += "; " + permission_parts.at(index);
        }
        lines.push_back("");
        lines.push_back("  Permission rule: " + trim_to_width(rule, width > 19 ? width - 19 : width));
    }
    lines.push_back("");
    lines.push_back(std::string{k_select_pointer} + "1. Yes, allow once (y)");
    lines.push_back("  2. Yes, allow for this session (a)");
    lines.push_back("  3. No, continue without allowing (d)");
    lines.push_back("");
    lines.push_back("  Press enter to confirm or esc to cancel");
    return lines;
}

auto render_question_lines(const QuestionModalState& modal, int width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    lines.push_back("");
    lines.push_back("  Agent is waiting for your input");
    lines.push_back("");
    lines.push_back("  " + trim_to_width(modal.question, width > 2 ? width - 2 : width));
    if (!modal.tool_name.empty())
    {
        lines.push_back("");
        lines.push_back("  Tool: " + trim_to_width(modal.tool_name, width > 8 ? width - 8 : width));
    }
    if (!modal.reason.empty())
    {
        lines.push_back("  Reason: " + trim_to_width(modal.reason, width > 10 ? width - 10 : width));
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
    lines.push_back(std::string{k_codex_prompt_prefix} + trim_to_width(modal.answer, width > 2 ? width - 2 : width));
    lines.push_back("  Shift+Enter newline \xc2\xb7 Enter submit \xc2\xb7 Esc cancel");
    return lines;
}

auto welcome_banner_element(const TuiDisplayConfig& config) -> Element
{
    using namespace ftxui;

    Elements inner;

    inner.push_back(hbox({
        text(">_ ") | dim,
        text(config.product_name) | bold,
        text(" (v" + config.version + ")") | dim,
    }));
    inner.push_back(text(" "));
    inner.push_back(hbox({
        text("model:     ") | dim,
        text(config.model),
        text("  "),
        text("/model") | color(TuiTheme::primary()) | bold,
        text(" to change") | dim,
    }));
    inner.push_back(hbox({
        text("directory: ") | dim,
        text(config.directory),
    }));

    // Build a bordered card with rounded corners (Codex-style)
    auto inner_width = 50;
    for (const auto& row : inner)
    {
        // Estimate width from string content (rough)
    }
    inner_width = std::max(inner_width, 40);

    std::string top_border = std::string{k_round_corner_tl};
    std::string bottom_border = std::string{k_round_corner_bl};
    for (int i = 0; i < inner_width; ++i)
    {
        top_border += k_box_horizontal;
        bottom_border += k_box_horizontal;
    }
    top_border += k_round_corner_tr;
    bottom_border += k_round_corner_br;

    Elements rows;
    rows.push_back(text(top_border) | color(TuiTheme::border_default()));
    for (auto& row : inner)
    {
        rows.push_back(hbox({
            text(std::string{k_box_vertical}) | color(TuiTheme::border_default()),
            text(" "),
            std::move(row),
            filler(),
            text(" "),
            text(std::string{k_box_vertical}) | color(TuiTheme::border_default()),
        }));
    }
    rows.push_back(text(bottom_border) | color(TuiTheme::border_default()));

    Elements outer;
    outer.push_back(vbox(std::move(rows)));
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
    const auto target_height = std::max(0, height);

    // Render all items using the element path for proper per-cell styling.
    // For height limiting, use the string path to count lines per cell,
    // then render only the visible cells via the element path.
    Elements all_cell_elements;
    all_cell_elements.reserve(transcript.size());
    std::vector<int> cell_line_counts;
    cell_line_counts.reserve(transcript.size());
    int total_lines = 0;

    for (const auto& item : transcript)
    {
        auto cell_lines = render_history_cell_lines(item, width);
        const auto cell_lines_count = static_cast<int>(cell_lines.size());
        cell_line_counts.push_back(cell_lines_count);
        total_lines += cell_lines_count;
        all_cell_elements.push_back(history_cell_element(item, width));
    }

    Elements rows;
    const auto padding_rows = std::max(0, target_height - total_lines);

    if (follow_transcript)
    {
        for (int index = 0; index < padding_rows; ++index)
        {
            rows.push_back(text(" "));
        }
    }

    if (total_lines <= target_height)
    {
        // All content fits - render everything
        for (auto& elem : all_cell_elements)
        {
            rows.push_back(std::move(elem));
        }
    }
    else if (follow_transcript)
    {
        // Show last N lines: work backwards from the end
        int remaining = target_height;
        int start_idx = static_cast<int>(transcript.size()) - 1;
        while (start_idx >= 0 && remaining > 0)
        {
            remaining -= cell_line_counts.at(static_cast<std::size_t>(start_idx));
            start_idx--;
        }
        // start_idx is now one before the first visible cell
        // If we went past, we need to trim the first visible cell
        for (int i = start_idx + 1; i < static_cast<int>(transcript.size()); ++i)
        {
            if (i == start_idx + 1 && remaining < 0)
            {
                // Partial first cell - use string rendering for the visible portion
                auto cell_lines = render_history_cell_lines(transcript.at(static_cast<std::size_t>(i)), width);
                const auto skip = static_cast<std::size_t>(-remaining);
                for (auto idx = skip; idx < cell_lines.size(); ++idx)
                {
                    rows.push_back(transcript_line_element(cell_lines.at(idx)));
                }
            }
            else
            {
                rows.push_back(std::move(all_cell_elements.at(static_cast<std::size_t>(i))));
            }
        }
    }
    else
    {
        // Show first N lines: work forwards from the start
        int remaining = target_height;
        int end_idx = 0;
        while (end_idx < static_cast<int>(transcript.size()) && remaining > 0)
        {
            remaining -= cell_line_counts.at(static_cast<std::size_t>(end_idx));
            end_idx++;
        }
        for (int i = 0; i < end_idx; ++i)
        {
            if (i == end_idx - 1 && remaining < 0)
            {
                // Partial last cell - use string rendering for the visible portion
                auto cell_lines = render_history_cell_lines(transcript.at(static_cast<std::size_t>(i)), width);
                const auto count = static_cast<std::size_t>(cell_line_counts.at(static_cast<std::size_t>(i)) + remaining);
                for (std::size_t idx = 0; idx < count && idx < cell_lines.size(); ++idx)
                {
                    rows.push_back(transcript_line_element(cell_lines.at(idx)));
                }
            }
            else
            {
                rows.push_back(std::move(all_cell_elements.at(static_cast<std::size_t>(i))));
            }
        }
    }

    if (!follow_transcript)
    {
        for (int index = 0; index < padding_rows; ++index)
        {
            rows.push_back(text(" "));
        }
    }

    return vbox(std::move(rows)) | size(HEIGHT, EQUAL, target_height);
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
    for (const auto& line : render_permission_lines(prompt, width))
    {
        if (line.find("Would you like") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::text_strong()) | bold);
        }
        else if (line.starts_with(std::string{k_select_pointer}))
        {
            rows.push_back(hbox({
                text(std::string{k_select_pointer}) | color(TuiTheme::primary()) | bold,
                text(line.substr(std::string{k_select_pointer}.size())) | color(TuiTheme::primary()) | bold,
            }));
        }
        else if (line.find("No,") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::error()));
        }
        else if (line.find("Yes,") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::success()));
        }
        else if (line.find("Reason:") != std::string::npos || line.find("Permission rule:") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::text_default()));
        }
        else
        {
            rows.push_back(text(line) | dim);
        }
    }
    return vbox(std::move(rows));
}

auto select_modal_element(const SelectModalState& modal, int width) -> Element
{
    return list_dialog_element(select_modal_spec(modal), width);
}

auto question_modal_element(const QuestionModalState& modal, int width) -> Element
{
    Elements rows;

    for (const auto& line : render_question_lines(modal, width))
    {
        if (line.find("Agent is waiting") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::warning()) | dim);
        }
        else if (line == "  " + modal.question || line.find("Tool:") != std::string::npos)
        {
            rows.push_back(text(line) | color(TuiTheme::text_strong()) | bold);
        }
        else if (line.starts_with(std::string{k_codex_prompt_prefix}))
        {
            const auto content = line.substr(std::string{k_codex_prompt_prefix}.size());
            if (content.empty())
            {
                rows.push_back(hbox({
                    text(std::string{k_codex_prompt_prefix}) | color(TuiTheme::codex_user_prefix()) | bold | dim,
                    text(" ") | inverted,
                }));
            }
            else
            {
                rows.push_back(hbox({
                    text(std::string{k_codex_prompt_prefix}) | color(TuiTheme::codex_user_prefix()) | bold | dim,
                    text(content.substr(0, content.size() - 1)),
                    text(std::string(1, content.back())) | inverted,
                }));
            }
        }
        else if (line.find("Reason:") != std::string::npos)
        {
            rows.push_back(text(line) | dim);
        }
        else
        {
            rows.push_back(text(line) | dim);
        }
    }
    return vbox(std::move(rows));
}

} // namespace codeharness::tui::render

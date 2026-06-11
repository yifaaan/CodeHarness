#include "codeharness/tui/tui_render.h"

#include "codeharness/tui/list_dialog_render.h"
#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <iterator>

namespace codeharness::tui::render
{
namespace
{

using namespace ftxui;

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

} // namespace

auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>
{
    return {
        "CodeHarness",
        "An AI-powered coding assistant  v" + config.version,
        "",
        "  /help commands  |  /skills list  |  Ctrl+C exit",
    };
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

    // Title
    rows.push_back(text("CodeHarness") | color(TuiTheme::primary()) | bold);
    rows.push_back(text("An AI-powered coding assistant  v" + config.version) | dim);
    rows.push_back(text(" "));

    // Key bindings hint row
    rows.push_back(hbox({
        text("  "),
        text("/help") | color(TuiTheme::primary()),
        text(" commands") | dim,
        text("  |  ") | dim,
        text("/skills") | color(TuiTheme::primary()),
        text(" list") | dim,
        text("  |  ") | dim,
        text("Ctrl+C") | color(TuiTheme::primary()),
        text(" exit") | dim,
    }));

    return vbox(std::move(rows));
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
    rows.reserve(lines.size() + 1);

    if (follow_transcript)
    {
        rows.push_back(filler());
    }
    for (const auto& line : lines)
    {
        rows.push_back(text(line));
    }

    return vbox(std::move(rows)) | flex;
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

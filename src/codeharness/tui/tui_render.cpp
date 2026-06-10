#include "codeharness/tui/tui_render.h"

#include "codeharness/tui/list_dialog_render.h"
#include "codeharness/tui/render_primitives.h"
#include "codeharness/tui/tui_theme.h"

#include <ftxui/dom/elements.hpp>

#include <iterator>
#include <sstream>
#include <string_view>

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

} // namespace

auto format_token_count(int count) -> std::string
{
    if (count >= 1000)
    {
        auto whole = count / 1000;
        auto tenths = (count % 1000) / 100;
        return std::to_string(whole) + "." + std::to_string(tenths) + "k";
    }
    return std::to_string(count);
}

auto render_welcome_lines(const TuiDisplayConfig& config) -> std::vector<std::string>
{
    return {
        "CodeHarness",
        "An AI-powered coding assistant  v" + config.version,
        "",
        "/help commands  |  /skills list  |  Ctrl+C exit",
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

auto render_status_footer_line(const TuiDisplayConfig& config, const TuiState& state) -> std::string
{
    std::ostringstream output;
    output << "model: " << config.model << " \xe2\x94\x82 provider: " << config.provider_type;
    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        output << " \xe2\x94\x82 tokens: " << format_token_count(config.token_usage.input_tokens)
               << "\xe2\x86\x93 " << format_token_count(config.token_usage.output_tokens) << "\xe2\x86\x91";
    }
    if (config.skill_count > 0)
    {
        output << " \xe2\x94\x82 skills: " << config.skill_count;
    }
    if (state.active_session)
    {
        output << " \xe2\x94\x82 session: " << state.active_session->session_id;
    }
    if (config.mcp_info.connected > 0)
    {
        output << " \xe2\x94\x82 mcp: " << config.mcp_info.connected;
        if (config.mcp_info.failed > 0)
        {
            output << "/" << config.mcp_info.failed;
        }
    }
    if (state.permission_mode != PermissionMode::Plan)
    {
        output << " \xe2\x94\x82 mode: " << codeharness::permission_mode_label(state.permission_mode);
    }
    return output.str();
}

auto render_composer_hint(bool busy, int history_index) -> std::string
{
    if (busy)
    {
        return "Esc stop \xc2\xb7 Ctrl+C stop";
    }
    auto hint = std::string{"Shift+Enter newline \xc2\xb7 Enter send \xc2\xb7 / commands \xc2\xb7 Ctrl+P/N history \xc2\xb7 Ctrl+C exit"};
    if (history_index >= 0)
    {
        hint += " \xc2\xb7 history " + std::to_string(history_index + 1);
    }
    return hint;
}

auto busy_spinner_frame(int frame) -> std::string
{
    static constexpr std::string_view frames[] = {
        "\xe2\xa0\x8b",
        "\xe2\xa0\x99",
        "\xe2\xa0\xb9",
        "\xe2\xa0\xb8",
        "\xe2\xa0\xbc",
        "\xe2\xa0\xb4",
        "\xe2\xa0\xa6",
        "\xe2\xa0\xa7",
        "\xe2\xa0\x87",
        "\xe2\xa0\x8f",
    };
    return std::string{frames[static_cast<std::size_t>(frame) % (sizeof(frames) / sizeof(frames[0]))]};
}

auto welcome_banner_element(const TuiDisplayConfig& config) -> Element
{
    const auto lines = render_welcome_lines(config);
    Elements rows;
    rows.push_back(text(std::string{lines.at(0)}) | color(TuiTheme::primary()) | bold);
    rows.push_back(text(lines.at(1)) | dim);
    rows.push_back(text(" "));
    rows.push_back(hbox({
        text(" "),
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

auto status_footer_element(const TuiDisplayConfig& config, const TuiState& state) -> Element
{
    Elements parts;

    parts.push_back(text("model: ") | color(TuiTheme::primary()) | dim);
    parts.push_back(text(config.model) | dim);
    parts.push_back(text(std::string{k_separator}) | dim);

    parts.push_back(text("provider: ") | dim);
    parts.push_back(text(config.provider_type) | dim);

    if (config.token_usage.input_tokens > 0 || config.token_usage.output_tokens > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("tokens: ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.input_tokens) + "\xe2\x86\x93 ") | dim);
        parts.push_back(text(format_token_count(config.token_usage.output_tokens) + "\xe2\x86\x91") | dim);
    }

    if (config.skill_count > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("skills: " + std::to_string(config.skill_count)) | dim);
    }

    if (state.active_session)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("session: " + state.active_session->session_id) | dim);
    }

    if (config.mcp_info.connected > 0)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        auto mcp_text = "mcp: " + std::to_string(config.mcp_info.connected);
        if (config.mcp_info.failed > 0)
        {
            mcp_text += "/" + std::to_string(config.mcp_info.failed);
        }
        parts.push_back(text(mcp_text) | dim);
    }

    if (state.permission_mode != PermissionMode::Plan)
    {
        parts.push_back(text(std::string{k_separator}) | dim);
        parts.push_back(text("mode: " + std::string{codeharness::permission_mode_label(state.permission_mode)}) | dim);
    }

    if (state.permission_mode == PermissionMode::Plan)
    {
        parts.push_back(text(" [PLAN MODE] ") | color(TuiTheme::warning()) | bold);
    }
    return hbox(std::move(parts));
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

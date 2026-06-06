#include "codeharness/tui/tui_app.h"
#include "test_support.h"

TEST_CASE("tui model renders pending permission modal")
{
    codeharness::tui::TuiAppModel model;
    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-1",
            .tool_use_id = "tool-use-1",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
            .path = std::filesystem::path{"output.txt"},
        });

    auto rendered = model.render_text();

    CHECK(rendered.find("Permission required") != std::string::npos);
    CHECK(rendered.find("write_file") != std::string::npos);
    CHECK(rendered.find("output.txt") != std::string::npos);
    CHECK(model.handle_permission_approve() == codeharness::tui::TuiAction::ApprovePermission);
    CHECK(model.handle_permission_deny() == codeharness::tui::TuiAction::DenyPermission);
}

TEST_CASE("tui model composer submit and quit states")
{
    codeharness::tui::TuiAppModel model;

    CHECK(model.handle_submit() == codeharness::tui::TuiAction::None);
    model.set_composer("hello tui");
    CHECK(model.handle_submit() == codeharness::tui::TuiAction::SubmitPrompt);

    model.begin_prompt("hello tui");
    CHECK(model.state().busy);
    CHECK(model.handle_quit() == codeharness::tui::TuiAction::None);

    model.complete_prompt();
    CHECK(!model.state().busy);
    CHECK(model.handle_quit() == codeharness::tui::TuiAction::Quit);
    CHECK(model.state().should_quit);
}

TEST_CASE("tui model interrupt marks busy run and renders status")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("hello tui");

    CHECK(model.handle_interrupt() == codeharness::tui::TuiAction::Interrupt);
    model.apply_engine_event(codeharness::EngineError{.message = "interrupted"});
    CHECK(model.state().interrupt_requested);
    CHECK(model.render_text().find("interrupted") != std::string::npos);
    REQUIRE(model.state().transcript.size() == 2);

    model.complete_prompt();
    CHECK(!model.state().busy);
    CHECK(!model.state().interrupt_requested);
}

TEST_CASE("tui model interrupt clears pending permission")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("write file");
    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-1",
            .tool_use_id = "tool-use-1",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
        });

    CHECK(model.handle_interrupt() == codeharness::tui::TuiAction::Interrupt);
    CHECK(!model.state().pending_permission.has_value());
    CHECK(model.state().interrupt_requested);
}

TEST_CASE("tui model command palette filters navigates and inserts")
{
    codeharness::tui::TuiAppModel model;
    model.set_composer("/");
    model.open_command_palette(
        {
            codeharness::tui::CommandPaletteEntry{
                .name = "resume",
                .description = "Resume a saved session.",
            },
            codeharness::tui::CommandPaletteEntry{
                .name = "sessions",
                .description = "List saved sessions.",
            },
            codeharness::tui::CommandPaletteEntry{
                .name = "skills",
                .description = "List loaded skills.",
            },
        });

    REQUIRE(model.state().command_palette.has_value());
    CHECK(model.state().command_palette->matches.size() == 3);

    model.command_palette_input('s');
    REQUIRE(model.state().command_palette.has_value());
    CHECK(model.state().composer == "/s");
    CHECK(model.state().command_palette->query == "s");
    CHECK(model.state().command_palette->matches.size() == 3);

    model.command_palette_input('k');
    REQUIRE(model.state().command_palette.has_value());
    CHECK(model.state().command_palette->matches.size() == 1);
    CHECK(model.selected_command_text() == "/skills ");
    CHECK(model.handle_command_select() == codeharness::tui::TuiAction::InsertCommand);
    CHECK(!model.state().command_palette.has_value());
    CHECK(model.state().composer == "/skills ");
}

TEST_CASE("tui model command palette escape clears query then closes")
{
    codeharness::tui::TuiAppModel model;
    model.set_composer("/");
    model.open_command_palette(
        {
            codeharness::tui::CommandPaletteEntry{
                .name = "skills",
                .description = "List loaded skills.",
            },
        });

    model.command_palette_input('z');
    REQUIRE(model.state().command_palette.has_value());
    CHECK(model.state().command_palette->matches.empty());

    CHECK(model.handle_command_cancel() == codeharness::tui::TuiAction::None);
    REQUIRE(model.state().command_palette.has_value());
    CHECK(model.state().composer == "/");
    CHECK(model.state().command_palette->query.empty());

    CHECK(model.handle_command_cancel() == codeharness::tui::TuiAction::None);
    CHECK(!model.state().command_palette.has_value());
}

TEST_CASE("tui model command palette is suppressed while busy or permission pending")
{
    codeharness::tui::TuiAppModel model;

    model.begin_prompt("hello");
    model.open_command_palette(
        {
            codeharness::tui::CommandPaletteEntry{
                .name = "skills",
                .description = "List loaded skills.",
            },
        });
    CHECK(!model.state().command_palette.has_value());
    model.complete_prompt();

    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-1",
            .tool_use_id = "tool-use-1",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
        });
    model.set_composer("/");
    model.open_command_palette(
        {
            codeharness::tui::CommandPaletteEntry{
                .name = "skills",
                .description = "List loaded skills.",
            },
        });
    CHECK(!model.state().command_palette.has_value());
}

TEST_CASE("tui model transcript handles engine events")
{
    codeharness::tui::TuiAppModel model;

    model.begin_prompt("run tool");
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = "hello"});
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = " world"});
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    model.apply_engine_event(
        codeharness::EngineToolResult{.id = "tool-use-1", .content = "done", .is_error = false});
    model.complete_prompt();

    REQUIRE(model.state().transcript.size() == 4);
    CHECK(model.state().transcript.at(0).kind == "user");
    CHECK(model.state().transcript.at(1).kind == "assistant");
    CHECK(model.state().transcript.at(1).text == "hello world");
    CHECK(model.state().transcript.at(2).kind == "tool");
    CHECK(model.state().transcript.at(3).kind == "tool_result");
}

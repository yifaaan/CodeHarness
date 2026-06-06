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

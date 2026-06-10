#include "codeharness/tui/tui_app.h"
#include "codeharness/tui/bottom_pane/bottom_pane.h"
#include "codeharness/tui/chat_surface.h"
#include "codeharness/tui/terminal.h"
#include "codeharness/tui/tui_composer.h"
#include "codeharness/tui/tui_event.h"
#include "codeharness/tui/tui_markdown.h"
#include "codeharness/tui/tui_render.h"
#include "test_support.h"

#include <ftxui/component/event.hpp>

TEST_CASE("tui event queue preserves fifo order and drains")
{
    codeharness::tui::TuiEventQueue queue;

    queue.push(codeharness::tui::TuiRefreshRequested{});
    queue.push(codeharness::tui::TuiRunCompleted{.success = true, .output_text = "done"});

    auto events = queue.drain();
    REQUIRE(events.size() == 2);
    CHECK(std::holds_alternative<codeharness::tui::TuiRefreshRequested>(events.at(0)));
    REQUIRE(std::holds_alternative<codeharness::tui::TuiRunCompleted>(events.at(1)));
    CHECK(std::get<codeharness::tui::TuiRunCompleted>(events.at(1)).output_text == "done");
    CHECK(queue.empty());
}

TEST_CASE("tui event sender wakes once per send")
{
    codeharness::tui::TuiEventQueue queue;
    int wake_count = 0;
    codeharness::tui::TuiEventSender sender{queue, [&] { ++wake_count; }};

    sender.send(codeharness::tui::TuiRefreshRequested{});
    sender.send(codeharness::tui::TuiRefreshRequested{});

    CHECK(wake_count == 2);
    CHECK(queue.drain().size() == 2);
}

TEST_CASE("tui event queue preserves engine event payload")
{
    codeharness::tui::TuiEventQueue queue;
    queue.push(codeharness::tui::TuiEngineEvent{
        .event = codeharness::EngineAssistantTextDelta{.text = "hello"},
    });

    auto events = queue.drain();
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<codeharness::tui::TuiEngineEvent>(events.at(0)));
    const auto& event = std::get<codeharness::tui::TuiEngineEvent>(events.at(0)).event;
    REQUIRE(std::holds_alternative<codeharness::EngineAssistantTextDelta>(event));
    CHECK(std::get<codeharness::EngineAssistantTextDelta>(event).text == "hello");
}

TEST_CASE("tui run completed event carries result fields")
{
    codeharness::tui::TuiRunCompleted completed{
        .success = false,
        .error_message = "failed",
        .output_text = "partial",
        .input_tokens = 12,
        .output_tokens = 34,
    };

    CHECK(!completed.success);
    CHECK(completed.error_message == "failed");
    CHECK(completed.output_text == "partial");
    CHECK(completed.input_tokens == 12);
    CHECK(completed.output_tokens == 34);
}

TEST_CASE("tui terminal alive gate starts alive and closes idempotently")
{
    codeharness::tui::TerminalAliveGate gate;

    CHECK(gate.is_alive());
    REQUIRE(gate.flag() != nullptr);

    gate.close();
    CHECK(!gate.is_alive());

    gate.close();
    CHECK(!gate.is_alive());
}

TEST_CASE("tui terminal alive gate only posts while alive")
{
    codeharness::tui::TerminalAliveGate gate;
    int post_count = 0;

    CHECK(gate.post_if_alive([&] { ++post_count; }));
    CHECK(post_count == 1);

    gate.close();
    CHECK(!gate.post_if_alive([&] { ++post_count; }));
    CHECK(post_count == 1);
}

TEST_CASE("tui terminal session exposes alive flag and close is idempotent")
{
    codeharness::tui::TuiTerminalSession terminal;

    CHECK(terminal.is_alive());
    REQUIRE(terminal.alive_flag() != nullptr);

    terminal.close();
    CHECK(!terminal.is_alive());

    terminal.close();
    CHECK(!terminal.is_alive());
}

TEST_CASE("tui terminal session exit loop is idempotent")
{
    codeharness::tui::TuiTerminalSession terminal;

    terminal.exit_loop();
    terminal.exit_loop();

    CHECK(terminal.is_alive());
    terminal.close();
    CHECK(!terminal.is_alive());
}

TEST_CASE("tui bottom pane command palette filters cancels and selects")
{
    codeharness::tui::BottomPane pane;
    pane.set_composer("/");
    pane.open_command_palette({
                                  codeharness::tui::CommandPaletteEntry{
                                      .name = "resume",
                                      .description = "Resume a saved session.",
                                  },
                                  codeharness::tui::CommandPaletteEntry{
                                      .name = "skills",
                                      .description = "List loaded skills.",
                                  },
                              },
                              false);

    REQUIRE(pane.state().command_palette.has_value());
    CHECK(pane.state().command_palette->matches.size() == 2);

    pane.command_palette_input('k', false);
    REQUIRE(pane.state().command_palette.has_value());
    CHECK(pane.state().composer == "/k");
    CHECK(pane.state().command_palette->query == "k");
    CHECK(pane.state().command_palette->matches.size() == 1);
    CHECK(pane.selected_command_text() == "/skills ");

    pane.handle_command_cancel();
    REQUIRE(pane.state().command_palette.has_value());
    CHECK(pane.state().composer == "/");
    CHECK(pane.state().command_palette->query.empty());

    CHECK(pane.handle_command_select());
    CHECK(!pane.state().command_palette.has_value());
    CHECK(pane.state().composer == "/resume ");
}

TEST_CASE("tui bottom pane select modal searches cancels and quick selects")
{
    codeharness::tui::BottomPane pane;
    pane.open_select_modal("Select model",
                           {
                               codeharness::tui::ModelOption{.value = "gpt-4", .label = "GPT-4", .description = "openai"},
                               codeharness::tui::ModelOption{.value = "echo", .label = "Echo", .description = "echo", .is_current = true},
                           },
                           false);

    REQUIRE(pane.state().select_modal.has_value());
    CHECK(pane.state().select_modal->cursor == 1);
    auto selected = pane.select_modal_current();
    REQUIRE(selected.has_value());
    CHECK(selected->value == "echo");

    pane.select_modal_input('g', false);
    REQUIRE(pane.state().select_modal.has_value());
    CHECK(pane.state().select_modal->query == "g");
    CHECK(pane.state().select_modal->matches.size() == 1);
    selected = pane.select_modal_current();
    REQUIRE(selected.has_value());
    CHECK(selected->value == "gpt-4");

    pane.handle_select_cancel();
    REQUIRE(pane.state().select_modal.has_value());
    CHECK(pane.state().select_modal->query.empty());
    CHECK(pane.state().select_modal->matches.size() == 2);

    auto quick = pane.select_modal_quick_select(2);
    REQUIRE(quick.has_value());
    CHECK(quick->value == "echo");

    pane.handle_select_cancel();
    CHECK(!pane.state().select_modal.has_value());
}

TEST_CASE("tui bottom pane permission and question flows")
{
    codeharness::tui::BottomPane pane;
    pane.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-1",
            .tool_use_id = "tool-use-1",
            .tool_name = "write_file",
            .reason = "confirm",
        });
    CHECK(pane.has_pending_permission());
    CHECK(pane.handle_permission_approve());
    CHECK(!pane.has_pending_permission());

    pane.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-2",
            .tool_use_id = "tool-use-2",
            .tool_name = "write_file",
            .reason = "confirm",
        });
    CHECK(pane.handle_permission_approve_for_session());

    pane.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-3",
            .tool_use_id = "tool-use-3",
            .tool_name = "write_file",
            .reason = "confirm",
        });
    CHECK(pane.handle_permission_deny());

    pane.show_question("q-1", "Which file?", "ask_user", "Need target");
    pane.question_modal_input('a');
    pane.question_modal_input('b');
    pane.question_modal_newline();
    pane.question_modal_input('c');
    CHECK(pane.question_modal_submit() == "ab\nc");
    pane.close_question();
    CHECK(!pane.state().question_modal.has_value());
}

TEST_CASE("tui bottom pane paste handling respects blockers")
{
    codeharness::tui::BottomPane pane;

    pane.detect_paste_burst("a");
    CHECK(!pane.state().paste_burst_active);
    pane.detect_paste_burst("hello");
    CHECK(pane.state().paste_burst_active);

    pane.set_composer("base ");
    pane.apply_paste_to_composer("\x1b[200~paste\x1b[201~", false);
    CHECK(pane.state().composer == "base paste");

    pane.show_question("q-1", "Question?", "ask_user", "reason");
    pane.apply_paste_to_composer(" blocked", false);
    CHECK(pane.state().composer == "base paste");
    pane.close_question();

    pane.apply_paste_to_composer(" busy", true);
    CHECK(pane.state().composer == "base paste");
}

TEST_CASE("tui chat surface streams assistant output and resets on new prompt")
{
    codeharness::tui::ChatSurface chat;

    chat.begin_prompt("run tool");
    CHECK(!chat.has_streamed_assistant_output());
    chat.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = "hello"});
    chat.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = " world"});

    REQUIRE(chat.items().size() == 2);
    CHECK(chat.has_streamed_assistant_output());
    CHECK(chat.items().at(0).kind == "user");
    CHECK(chat.items().at(1).kind == "assistant");
    CHECK(chat.items().at(1).text == "hello world");

    chat.begin_prompt("next");
    CHECK(!chat.has_streamed_assistant_output());
}

TEST_CASE("tui chat surface merges tool events and toggles details")
{
    codeharness::tui::ChatSurface chat;
    chat.begin_prompt("run tool");

    chat.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    chat.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "done", .is_error = false});
    chat.apply_engine_event(codeharness::EngineToolFinished{.id = "tool-use-1"});

    REQUIRE(chat.items().size() == 2);
    CHECK(chat.items().at(1).kind == "tool");
    CHECK(chat.items().at(1).id == "tool-use-1");
    CHECK(chat.items().at(1).text == "bash completed");
    CHECK(chat.items().at(1).detail == "done");
    CHECK(!chat.items().at(1).expanded);

    CHECK(!chat.toggle_tool_details(0));
    CHECK(chat.toggle_tool_details(1));
    CHECK(chat.items().at(1).expanded);
}

TEST_CASE("tui chat surface merges duplicate tool starts")
{
    codeharness::tui::ChatSurface chat;
    chat.begin_prompt("run tool");

    chat.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    chat.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "read_file"});

    REQUIRE(chat.items().size() == 2);
    CHECK(chat.items().at(1).kind == "tool");
    CHECK(chat.items().at(1).id == "tool-use-1");
    CHECK(chat.items().at(1).text == "read_file running");
}

TEST_CASE("tui chat surface records orphan and failed tool results")
{
    codeharness::tui::ChatSurface chat;
    chat.begin_prompt("orphan result");

    chat.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-2", .content = "exit 1", .is_error = true});

    REQUIRE(chat.items().size() == 2);
    CHECK(chat.items().at(1).kind == "tool");
    CHECK(chat.items().at(1).id == "tool-use-2");
    CHECK(chat.items().at(1).text == "failed");
    CHECK(chat.items().at(1).detail == "exit 1");
    CHECK(chat.items().at(1).is_error);
    CHECK(chat.items().at(1).expanded);
}

TEST_CASE("tui chat surface suppresses duplicate errors")
{
    codeharness::tui::ChatSurface chat;

    chat.append_error_once("interrupted");
    chat.append_error_once("interrupted");
    chat.apply_engine_event(codeharness::EngineError{.message = "interrupted"});

    REQUIRE(chat.items().size() == 1);
    CHECK(chat.items().at(0).kind == "error");
    CHECK(chat.items().at(0).text == "interrupted");
}

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

    CHECK(rendered.find("Allow write_file?") != std::string::npos);
    CHECK(rendered.find("write_file") != std::string::npos);
    CHECK(rendered.find("output.txt") != std::string::npos);
    CHECK(rendered.find("allow once") != std::string::npos);
    CHECK(rendered.find("allow session") != std::string::npos);
    CHECK(model.handle_permission_approve() == codeharness::tui::TuiAction::ApprovePermission);
    CHECK(!model.state().pending_permission.has_value());

    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-2",
            .tool_use_id = "tool-use-2",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
        });
    CHECK(model.handle_permission_approve_for_session() == codeharness::tui::TuiAction::ApprovePermissionForSession);
    CHECK(!model.state().pending_permission.has_value());

    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-3",
            .tool_use_id = "tool-use-3",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
        });
    CHECK(model.handle_permission_deny() == codeharness::tui::TuiAction::DenyPermission);
    CHECK(!model.state().pending_permission.has_value());
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

TEST_CASE("tui busy state suppresses prompt submit but allows interrupt")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("running");

    model.set_composer("new prompt");
    CHECK(model.handle_submit() == codeharness::tui::TuiAction::None);
    CHECK(model.handle_quit() == codeharness::tui::TuiAction::None);
    CHECK(model.handle_interrupt() == codeharness::tui::TuiAction::Interrupt);
    CHECK(model.state().interrupt_requested);
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
    model.open_command_palette({
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
    model.open_command_palette({
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
    model.open_command_palette({
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
    model.open_command_palette({
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
    CHECK(!model.has_streamed_assistant_output());
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = "hello"});
    CHECK(model.has_streamed_assistant_output());
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = " world"});
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "done", .is_error = false});
    model.complete_prompt();

    REQUIRE(model.state().transcript.size() == 3);
    CHECK(model.state().transcript.at(0).kind == "user");
    CHECK(model.state().transcript.at(1).kind == "assistant");
    CHECK(model.state().transcript.at(1).text == "hello world");
    CHECK(model.state().transcript.at(2).kind == "tool");
    CHECK(model.state().transcript.at(2).id == "tool-use-1");
    CHECK(model.state().transcript.at(2).text == "bash completed");
    CHECK(model.state().transcript.at(2).detail == "done");
    CHECK(!model.state().transcript.at(2).expanded);
    CHECK(model.render_text().find("done") == std::string::npos);
    CHECK(model.toggle_tool_details(2));
    CHECK(model.state().transcript.at(2).expanded);
    CHECK(model.render_text().find("Ran bash 1L") != std::string::npos);
    CHECK(model.render_text().find("done") != std::string::npos);

    model.begin_prompt("next prompt");
    CHECK(!model.has_streamed_assistant_output());
}

TEST_CASE("tui model merges finished and error tool events")
{
    codeharness::tui::TuiAppModel model;

    model.begin_prompt("run failing tool");
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    model.apply_engine_event(codeharness::EngineToolFinished{.id = "tool-use-1"});
    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "exit 1", .is_error = true});

    REQUIRE(model.state().transcript.size() == 2);
    CHECK(model.state().transcript.at(1).kind == "tool");
    CHECK(model.state().transcript.at(1).id == "tool-use-1");
    CHECK(model.state().transcript.at(1).text == "bash failed");
    CHECK(model.state().transcript.at(1).detail == "exit 1");
    CHECK(model.state().transcript.at(1).is_error);
}

TEST_CASE("tui model records missing tool result as a tool row")
{
    codeharness::tui::TuiAppModel model;

    model.begin_prompt("orphan result");
    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-2", .content = "failed", .is_error = true});

    REQUIRE(model.state().transcript.size() == 2);
    CHECK(model.state().transcript.at(1).kind == "tool");
    CHECK(model.state().transcript.at(1).id == "tool-use-2");
    CHECK(model.state().transcript.at(1).text == "failed");
    CHECK(model.state().transcript.at(1).detail == "failed");
    CHECK(model.state().transcript.at(1).is_error);
}

TEST_CASE("tui render uses codex style transcript and command palette")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("fix bug");
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = "Done."});
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "ok", .is_error = false});
    model.complete_prompt();

    const auto rendered = model.render_text();
    CHECK(rendered.find("> fix bug") != std::string::npos);
    CHECK(rendered.find("Done.") != std::string::npos);
    CHECK(rendered.find("• Ran bash 1L") != std::string::npos);

    model.set_composer("/");
    model.open_command_palette({
        codeharness::tui::CommandPaletteEntry{
            .name = "skills",
            .description = "List loaded skills.",
        },
    });
    const auto palette_rendered = model.render_text();
    CHECK(palette_rendered.find("Select a command  (type to search)") != std::string::npos);
    CHECK(palette_rendered.find("\xe2\x9d\xaf /skills") != std::string::npos);
    CHECK(palette_rendered.find("↑↓ navigate · Enter select · Esc cancel") != std::string::npos);
}

TEST_CASE("tui command palette renders search empty and overflow states")
{
    codeharness::tui::TuiAppModel model;
    std::vector<codeharness::tui::CommandPaletteEntry> commands;
    for (int index = 0; index < 10; ++index)
    {
        commands.push_back(codeharness::tui::CommandPaletteEntry{
            .name = "cmd" + std::to_string(index),
            .description = "Command " + std::to_string(index),
        });
    }

    model.set_composer("/");
    model.open_command_palette(commands);
    auto rendered = model.render_text();
    CHECK(rendered.find("Select a command  (type to search)") != std::string::npos);
    CHECK(rendered.find("↑↓ navigate · Enter select · Esc cancel") != std::string::npos);
    CHECK(rendered.find("▼ 2 more") != std::string::npos);

    model.command_palette_input('z');
    rendered = model.render_text();
    CHECK(rendered.find("Search: z") != std::string::npos);
    CHECK(rendered.find("Backspace clear") != std::string::npos);
    CHECK(rendered.find("No matches") != std::string::npos);
}

TEST_CASE("tui render auto expands failed tool output")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("run failing tool");
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});
    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "exit 1", .is_error = true});

    CHECK(model.state().transcript.at(1).expanded);
    CHECK(model.render_text().find("• Ran bash error") != std::string::npos);
    CHECK(model.render_text().find("exit 1") != std::string::npos);
}

TEST_CASE("tui model only toggles expandable tool rows")
{
    codeharness::tui::TuiAppModel model;

    model.begin_prompt("toggle rows");
    model.apply_engine_event(codeharness::EngineAssistantTextDelta{.text = "answer"});
    model.apply_engine_event(codeharness::EngineToolStarted{.id = "tool-use-1", .name = "bash"});

    CHECK(!model.toggle_tool_details(0));
    CHECK(!model.toggle_tool_details(1));
    CHECK(!model.toggle_tool_details(2));

    model.apply_engine_event(codeharness::EngineToolResult{.id = "tool-use-1", .content = "output", .is_error = false});
    CHECK(model.toggle_tool_details(2));
    CHECK(model.state().transcript.at(2).expanded);
    CHECK(model.toggle_tool_details(2));
    CHECK(!model.state().transcript.at(2).expanded);
}

TEST_CASE("tui composer supports multiline editing and history")
{
    codeharness::tui::ComposerState composer;
    composer.insert_character('h');
    composer.insert_character('i');
    composer.insert_newline();
    composer.insert_character('!');
    CHECK(composer.content() == "hi\n!");

    composer.push_history("first");
    composer.push_history("second");
    composer.set_content("draft");
    composer.history_previous();
    CHECK(composer.content() == "second");
    composer.history_previous();
    CHECK(composer.content() == "first");
    composer.history_next();
    CHECK(composer.content() == "second");
    composer.history_next();
    CHECK(composer.content() == "draft");
}

TEST_CASE("tui composer only treats shifted enter sequences as newline")
{
    CHECK(!codeharness::tui::is_composer_newline_event(ftxui::Event::Return));
    CHECK(!codeharness::tui::is_composer_newline_event(ftxui::Event::CtrlJ));
    CHECK(!codeharness::tui::is_composer_newline_event(ftxui::Event::CtrlM));
    CHECK(codeharness::tui::is_composer_newline_event(ftxui::Event::Special("\x1b[13;2u")));
    CHECK(codeharness::tui::is_composer_newline_event(ftxui::Event::Special("\x1b[27;2~")));
}

TEST_CASE("tui composer hint matches codex style")
{
    CHECK(codeharness::tui::render::render_composer_hint(false) ==
          "Shift+Enter newline · Enter send · / commands · Ctrl+P/N history · Ctrl+C exit");
    CHECK(codeharness::tui::render::render_composer_hint(true) == "Esc stop · Ctrl+C stop");
    CHECK(codeharness::tui::render::render_composer_hint(false, 1).find("history 2") != std::string::npos);
}

TEST_CASE("tui markdown parses headings lists and inline styles")
{
    const auto blocks = codeharness::tui::markdown::parse_blocks("# Title\n\n- item\n\n`code` and **bold**");
    REQUIRE(blocks.size() == 5);
    CHECK(blocks.at(0).kind == codeharness::tui::markdown::BlockKind::heading1);
    CHECK(blocks.at(0).text == "Title");
    CHECK(blocks.at(1).kind == codeharness::tui::markdown::BlockKind::blank);
    CHECK(blocks.at(2).kind == codeharness::tui::markdown::BlockKind::bullet);
    CHECK(blocks.at(4).text == "`code` and **bold**");

    const auto plain = codeharness::tui::markdown::render_plain_text("## Section\n\nhello", 80);
    CHECK(plain.find("Section") != std::string::npos);
    CHECK(plain.find("hello") != std::string::npos);
}

TEST_CASE("tui model system messages and plan mode footer")
{
    codeharness::tui::TuiAppModel model;
    model.append_system_message("Entered plan mode.");
    model.set_permission_mode(codeharness::PermissionMode::Plan);

    const auto rendered = model.render_text();
    CHECK(rendered.find("[system] Entered plan mode.") != std::string::npos);

    const auto footer = codeharness::tui::render::render_status_footer_line(
        codeharness::tui::TuiDisplayConfig{.skill_count = 3},
        model.state());
    CHECK(footer.find("skills: 3") != std::string::npos);
    CHECK(footer.find("mode:") == std::string::npos);
}

// --- New feature tests ---

TEST_CASE("tui select modal opens navigates and selects")
{
    codeharness::tui::TuiAppModel model;
    model.set_composer("");

    model.open_select_modal("Select model", {
        codeharness::tui::ModelOption{.value = "gpt-4", .label = "GPT-4", .description = "openai"},
        codeharness::tui::ModelOption{.value = "claude-3", .label = "Claude 3", .description = "anthropic", .is_current = true},
        codeharness::tui::ModelOption{.value = "echo", .label = "Echo", .description = "echo"},
    });

    REQUIRE(model.state().select_modal.has_value());
    CHECK(model.state().select_modal->title == "Select model");
    CHECK(model.state().select_modal->options.size() == 3);
    CHECK(model.state().select_modal->matches.size() == 3);
    // Cursor should start at the current item (Claude 3, index 1)
    CHECK(model.state().select_modal->cursor == 1);

    // Navigate up
    model.select_modal_up();
    CHECK(model.state().select_modal->cursor == 0);

    // Navigate down
    model.select_modal_down();
    CHECK(model.state().select_modal->cursor == 1);
    model.select_modal_down();
    CHECK(model.state().select_modal->cursor == 2);
    // Wrap around
    model.select_modal_down();
    CHECK(model.state().select_modal->cursor == 0);

    // Get current selection
    auto selected = model.select_modal_current();
    REQUIRE(selected.has_value());
    CHECK(selected->value == "gpt-4");
    CHECK(selected->label == "GPT-4");

    // Quick select by digit
    auto quick = model.select_modal_quick_select(2);
    REQUIRE(quick.has_value());
    CHECK(quick->value == "claude-3");

    // Out of range quick select returns nullopt
    auto invalid = model.select_modal_quick_select(4);
    CHECK(!invalid.has_value());

    // Close
    model.close_select_modal();
    CHECK(!model.state().select_modal.has_value());
}

TEST_CASE("tui select modal uses list dialog style and supports search cancel")
{
    codeharness::tui::TuiAppModel model;
    model.open_select_modal("Select model", {
        codeharness::tui::ModelOption{.value = "gpt-4", .label = "GPT-4", .description = "openai"},
        codeharness::tui::ModelOption{.value = "echo", .label = "Echo", .description = "echo", .is_current = true},
    });

    auto rendered = model.render_text();
    CHECK(rendered.find("Select model  (type to search)") != std::string::npos);
    CHECK(rendered.find("↑↓ navigate · Enter select · Esc cancel") != std::string::npos);
    CHECK(rendered.find("\xe2\x9d\xaf Echo  echo") != std::string::npos);
    CHECK(rendered.find("\xe2\x86\x90 current") != std::string::npos);

    model.select_modal_input('g');
    rendered = model.render_text();
    CHECK(rendered.find("Search: g") != std::string::npos);
    CHECK(rendered.find("Backspace clear") != std::string::npos);
    REQUIRE(model.state().select_modal.has_value());
    CHECK(model.state().select_modal->matches.size() == 1);
    auto selected = model.select_modal_current();
    REQUIRE(selected.has_value());
    CHECK(selected->value == "gpt-4");

    model.handle_select_cancel();
    REQUIRE(model.state().select_modal.has_value());
    CHECK(model.state().select_modal->query.empty());
    CHECK(model.state().select_modal->matches.size() == 2);
    model.handle_select_cancel();
    CHECK(!model.state().select_modal.has_value());
}

TEST_CASE("tui select modal is suppressed while busy or permission pending")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("busy");

    model.open_select_modal("Select model", {
        codeharness::tui::ModelOption{.value = "echo", .label = "Echo"},
    });
    CHECK(!model.state().select_modal.has_value());

    model.complete_prompt();
    model.show_permission(
        codeharness::PermissionPrompt{
            .id = "perm-1",
            .tool_use_id = "tu-1",
            .tool_name = "bash",
            .reason = "test",
        });

    model.open_select_modal("Select model", {
        codeharness::tui::ModelOption{.value = "echo", .label = "Echo"},
    });
    CHECK(!model.state().select_modal.has_value());
}

TEST_CASE("tui question modal accepts input and submits")
{
    codeharness::tui::TuiAppModel model;
    model.show_question("q-1", "What file to edit?", "ask_user", "Need user input");

    REQUIRE(model.state().question_modal.has_value());
    CHECK(model.state().question_modal->question == "What file to edit?");
    CHECK(model.state().question_modal->tool_name == "ask_user");
    CHECK(model.state().question_modal->answer.empty());

    // Type answer
    model.question_modal_input('h');
    model.question_modal_input('i');
    CHECK(model.state().question_modal->answer == "hi");

    // Backspace
    model.question_modal_backspace();
    CHECK(model.state().question_modal->answer == "h");

    // Newline adds current answer to extra_lines
    model.question_modal_input('i');
    model.question_modal_newline();
    CHECK(model.state().question_modal->extra_lines.size() == 1);
    CHECK(model.state().question_modal->extra_lines.at(0) == "hi");
    CHECK(model.state().question_modal->answer.empty());

    // Submit combines extra_lines + current answer
    model.question_modal_input('!');
    auto answer = model.question_modal_submit();
    CHECK(answer == "hi\n!");

    // Close
    model.close_question();
    CHECK(!model.state().question_modal.has_value());
}

TEST_CASE("tui interrupt clears question modal")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("ask");
    model.show_question("ask-1", "Which file?", "ask_user", "Need target");

    CHECK(model.handle_interrupt() == codeharness::tui::TuiAction::Interrupt);
    CHECK(!model.state().question_modal.has_value());
    CHECK(model.state().interrupt_requested);
}

TEST_CASE("tui paste burst detection")
{
    codeharness::tui::TuiAppModel model;

    // Single character is not a paste burst
    model.detect_paste_burst("a");
    CHECK(!model.state().paste_burst_active);

    // Multi-character input is a paste burst
    model.detect_paste_burst("hello world");
    CHECK(model.state().paste_burst_active);

    // Bracketed paste start is detected even in single-char
    model.detect_paste_burst("\x1b[200~");
    CHECK(model.state().paste_burst_active);
}

TEST_CASE("tui paste applies to composer")
{
    codeharness::tui::TuiAppModel model;
    model.set_composer("existing ");

    model.apply_paste_to_composer("pasted text");
    CHECK(model.state().composer == "existing pasted text");
}

TEST_CASE("tui paste strips bracketed paste markers")
{
    codeharness::tui::TuiAppModel model;
    model.set_composer("");

    model.apply_paste_to_composer("\x1b[200~content here\x1b[201~");
    CHECK(model.state().composer == "content here");
}

TEST_CASE("tui paste is suppressed while busy")
{
    codeharness::tui::TuiAppModel model;
    model.begin_prompt("busy");
    model.apply_paste_to_composer("should not appear");
    CHECK(model.state().composer.empty());
}

TEST_CASE("tui transcript follow wheel behavior")
{
    CHECK(!codeharness::tui::apply_transcript_follow_wheel(true, true, false));
    CHECK(codeharness::tui::apply_transcript_follow_wheel(false, false, true));
    CHECK(codeharness::tui::apply_transcript_follow_wheel(true, false, false));
    CHECK(!codeharness::tui::apply_transcript_follow_wheel(false, false, false));
}

TEST_CASE("tui footer shows token usage and mcp connections")
{
    codeharness::tui::TuiDisplayConfig config{
        .model = "gpt-4",
        .provider_type = "openai",
        .skill_count = 2,
        .token_usage = codeharness::tui::TokenUsage{.input_tokens = 1500, .output_tokens = 800},
        .mcp_info = codeharness::tui::McpConnectionInfo{.connected = 3, .failed = 1},
    };
    codeharness::tui::TuiState state;

    const auto footer = codeharness::tui::render::render_status_footer_line(config, state);
    CHECK(footer.find("model: gpt-4") != std::string::npos);
    CHECK(footer.find("provider: openai") != std::string::npos);
    CHECK(footer.find("tokens:") != std::string::npos);
    CHECK(footer.find("1.5k") != std::string::npos);
    CHECK(footer.find("800") != std::string::npos);
    CHECK(footer.find("skills: 2") != std::string::npos);
    CHECK(footer.find("mcp: 3/1") != std::string::npos);
    CHECK(footer.find("mode: default") != std::string::npos);
}

TEST_CASE("tui footer reflects switched model profile")
{
    codeharness::tui::TuiDisplayConfig config{
        .model = "echo-b",
        .provider_type = "echo / echo-b",
    };
    codeharness::tui::TuiState state;

    const auto footer = codeharness::tui::render::render_status_footer_line(config, state);
    CHECK(footer.find("model: echo-b") != std::string::npos);
    CHECK(footer.find("provider: echo / echo-b") != std::string::npos);
}

TEST_CASE("tui footer shows active resumed session")
{
    codeharness::tui::TuiAppModel model;
    model.set_active_session(codeharness::SessionCommandSummary{
        .session_id = "abc123",
        .model = "echo",
        .summary = "old work",
        .message_count = 2,
    });

    const auto footer = codeharness::tui::render::render_status_footer_line(
        codeharness::tui::TuiDisplayConfig{.model = "echo", .provider_type = "echo"},
        model.state());

    CHECK(footer.find("session: abc123") != std::string::npos);
}

TEST_CASE("tui footer shows full auto permission mode")
{
    codeharness::tui::TuiState state;
    state.permission_mode = codeharness::PermissionMode::FullAuto;

    const auto footer = codeharness::tui::render::render_status_footer_line(
        codeharness::tui::TuiDisplayConfig{.model = "echo", .provider_type = "echo"},
        state);

    CHECK(footer.find("mode: full_auto") != std::string::npos);
}

TEST_CASE("tui footer hides token and mcp when zero")
{
    codeharness::tui::TuiDisplayConfig config{
        .model = "echo",
        .provider_type = "echo",
    };
    codeharness::tui::TuiState state;

    const auto footer = codeharness::tui::render::render_status_footer_line(config, state);
    CHECK(footer.find("tokens:") == std::string::npos);
    CHECK(footer.find("mcp:") == std::string::npos);
}

TEST_CASE("tui format token count")
{
    CHECK(codeharness::tui::render::format_token_count(0) == "0");
    CHECK(codeharness::tui::render::format_token_count(500) == "500");
    CHECK(codeharness::tui::render::format_token_count(1000) == "1.0k");
    CHECK(codeharness::tui::render::format_token_count(1500) == "1.5k");
    CHECK(codeharness::tui::render::format_token_count(12345) == "12.3k");
}

TEST_CASE("tui markdown parses tables")
{
    const auto blocks = codeharness::tui::markdown::parse_blocks(
        "| Name  | Value |\n"
        "|-------|-------|\n"
        "| foo   | 1     |\n"
        "| bar   | 2     |");

    REQUIRE(blocks.size() == 1);
    CHECK(blocks.at(0).kind == codeharness::tui::markdown::BlockKind::table);

    // Verify plain text rendering includes the table content
    const auto plain = codeharness::tui::markdown::render_plain_text(
        "| Name  | Value |\n"
        "|-------|-------|\n"
        "| foo   | 1     |\n", 80);
    CHECK(plain.find("Name") != std::string::npos);
    CHECK(plain.find("Value") != std::string::npos);
    CHECK(plain.find("foo") != std::string::npos);
}

TEST_CASE("tui markdown parse_table helper")
{
    auto [table, next_line] = codeharness::tui::markdown::parse_table(
        "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n\n", 0);

    CHECK(table.header.cells.size() == 2);
    CHECK(table.header.cells.at(0).text == "A");
    CHECK(table.header.cells.at(1).text == "B");
    REQUIRE(table.rows.size() == 2);
    CHECK(table.rows.at(0).cells.at(0).text == "1");
    CHECK(table.rows.at(1).cells.at(1).text == "4");
}

TEST_CASE("tui markdown compute_col_widths")
{
    codeharness::tui::markdown::TableBlock table;
    table.header.cells = {
        codeharness::tui::markdown::TableCell{.text = "Name"},
        codeharness::tui::markdown::TableCell{.text = "Value"},
    };
    table.rows.push_back(codeharness::tui::markdown::TableRow{
        .cells = {
            codeharness::tui::markdown::TableCell{.text = "LongerName"},
            codeharness::tui::markdown::TableCell{.text = "x"},
        },
    });

    const auto widths = codeharness::tui::markdown::compute_col_widths(table);
    REQUIRE(widths.size() == 2);
    CHECK(widths.at(0) == 10); // "LongerName" is wider
    CHECK(widths.at(1) == 5);  // "Value" is wider
}

TEST_CASE("tui markdown parses horizontal rules")
{
    const auto blocks = codeharness::tui::markdown::parse_blocks("---\n\n***\n\n___");
    REQUIRE(blocks.size() == 5);
    CHECK(blocks.at(0).kind == codeharness::tui::markdown::BlockKind::horizontal_rule);
    CHECK(blocks.at(2).kind == codeharness::tui::markdown::BlockKind::horizontal_rule);
    CHECK(blocks.at(4).kind == codeharness::tui::markdown::BlockKind::horizontal_rule);
}

TEST_CASE("tui markdown code fence with language")
{
    const auto blocks = codeharness::tui::markdown::parse_blocks("```cpp\nint x = 0;\n```");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks.at(0).kind == codeharness::tui::markdown::BlockKind::code_fence);
    CHECK(blocks.at(0).language == "cpp");
    CHECK(blocks.at(0).text == "int x = 0;");
}

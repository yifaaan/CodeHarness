#include "codeharness/runtime/runtime.h"

#include "test_support.h"

#include <algorithm>
#include <string_view>
#include <variant>

namespace
{

auto make_bundle(TempDir& temp, codeharness::PermissionMode mode = codeharness::PermissionMode::Default)
    -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "repo";
    const auto memory_root = temp.path / "memory-root";
    std::filesystem::create_directories(repo);

    return codeharness::runtime::create_runtime_bundle(
        codeharness::runtime::RuntimeBundleOptions{
            .cwd = repo,
            .memory_root = memory_root,
            .permission_mode = mode,
            .load_default_user_plugins = false,
        });
}

auto contains_tool(const codeharness::ToolRegistry& tools, std::string_view name) -> bool
{
    const auto names = tools.names();
    return std::ranges::find(names, std::string{name}) != names.end();
}

} // namespace

TEST_CASE("runtime bundle creates coding agent runtime for a cwd")
{
    TempDir temp{"codeharness-runtime-create-test"};

    auto bundle = make_bundle(temp);

    REQUIRE(bundle.has_value());
    CHECK((*bundle)->cwd() == temp.path / "repo");
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::Default);
    CHECK((*bundle)->commands().lookup("/skills").command != nullptr);
}

TEST_CASE("runtime bundle registers built-in coding agent tools")
{
    TempDir temp{"codeharness-runtime-tools-test"};

    auto bundle = make_bundle(temp);

    REQUIRE(bundle.has_value());
    CHECK(contains_tool((*bundle)->tools(), "read_file"));
    CHECK(contains_tool((*bundle)->tools(), "edit_file"));
    CHECK(contains_tool((*bundle)->tools(), "glob"));
    CHECK(contains_tool((*bundle)->tools(), "grep"));
    CHECK(contains_tool((*bundle)->tools(), "bash"));
    CHECK(contains_tool((*bundle)->tools(), "skill"));
    CHECK(contains_tool((*bundle)->tools(), "task_create"));
    CHECK(contains_tool((*bundle)->tools(), "task_list"));
    CHECK(contains_tool((*bundle)->tools(), "task_get"));
    CHECK(contains_tool((*bundle)->tools(), "task_output"));
    CHECK(contains_tool((*bundle)->tools(), "task_stop"));
    CHECK(contains_tool((*bundle)->tools(), "agent"));
    CHECK(contains_tool((*bundle)->tools(), "send_message"));
}

TEST_CASE("runtime build_run_request includes prompt options context memory and permissions")
{
    TempDir temp{"codeharness-runtime-request-test"};

    auto bundle = make_bundle(temp, codeharness::PermissionMode::Plan);
    REQUIRE(bundle.has_value());

    REQUIRE((*bundle)
                ->memory_store()
                .add(
                    codeharness::memory::AddMemoryRequest{
                        .title = "Build Notes",
                        .body = "Use CMake when building CodeHarness.",
                    })
                .has_value());

    {
        std::ofstream file{(*bundle)->cwd() / "AGENTS.md", std::ios::binary};
        file << "runtime test project instructions";
    }

    auto request = (*bundle)->build_run_request("How should I build with CMake?", 7);

    REQUIRE(request.has_value());
    CHECK(request->prompt == "How should I build with CMake?");
    CHECK(request->options.max_turns == 7);
    REQUIRE(request->system_prompt.has_value());
    CHECK(request->system_prompt->find("# Environment") != std::string::npos);
    CHECK(request->system_prompt->find("# Permission Mode") != std::string::npos);
    CHECK(request->system_prompt->find("Plan: treat the session as read-only planning") != std::string::npos);
    CHECK(request->system_prompt->find("# Available Skills") != std::string::npos);
    CHECK(request->system_prompt->find("# Available Slash Commands") != std::string::npos);
    CHECK(request->system_prompt->find("- /skills: List loaded skills.") != std::string::npos);
    CHECK(request->system_prompt->find("# Project Context") != std::string::npos);
    CHECK(request->system_prompt->find("runtime test project instructions") != std::string::npos);
    CHECK(request->system_prompt->find("# Relevant Memories") != std::string::npos);
    CHECK(request->system_prompt->find("## Build Notes") != std::string::npos);
    CHECK(request->system_prompt->find("Use CMake when building CodeHarness.") != std::string::npos);
}

TEST_CASE("runtime slash command registry exposes message-only skills command")
{
    TempDir temp{"codeharness-runtime-command-test"};

    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto result = codeharness::execute_slash_command((*bundle)->commands(), "/skills");

    REQUIRE(result.has_value());
    REQUIRE(result->message.has_value());
    CHECK(result->message->find("Available skills:") != std::string::npos);
    CHECK(!result->submit_prompt.has_value());
}

TEST_CASE("runtime run_prompt forwards prompt through engine and streams assistant deltas")
{
    TempDir temp{"codeharness-runtime-run-test"};

    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    std::string streamed_text;
    auto result = (*bundle)->run_prompt("hello runtime", 3, [&](const codeharness::EngineEvent& event) {
        if (const auto* delta = std::get_if<codeharness::EngineAssistantTextDelta>(&event))
        {
            streamed_text += delta->text;
        }
    });

    REQUIRE(result.has_value());
    CHECK(streamed_text == "hello runtime");
    CHECK(result->output_text == "hello runtime");
    REQUIRE(result->messages.size() >= 2);
    CHECK(result->messages.front().role == codeharness::Role::System);
}

TEST_CASE("runtime interrupted run does not save a session")
{
    TempDir temp{"codeharness-runtime-interrupt-no-save-test"};

    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::CancellationSource cancellation;
    cancellation.cancel();

    auto result = (*bundle)->run_prompt(
        "do not save",
        codeharness::runtime::RunPromptOptions{
            .max_turns = 3,
            .cancellation = cancellation.token(),
        },
        {});

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Cancelled);

    auto sessions = (*bundle)->sessions().list(10);
    REQUIRE(sessions.has_value());
    CHECK(sessions->empty());
}

TEST_CASE("runtime resumes a saved session by id and exposes summary")
{
    TempDir temp{"codeharness-runtime-resume-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "resume-me";
    snapshot.cwd = (*bundle)->cwd();
    snapshot.model = "echo";
    snapshot.summary = "previous prompt";
    snapshot.message_count = 2;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::System, "old system"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "previous prompt"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::Assistant, "previous answer"));
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto resumed = (*bundle)->resume_session("resume-me");
    REQUIRE(resumed.has_value());
    CHECK(resumed->session_id == "resume-me");
    CHECK(resumed->summary == "previous prompt");

    auto active = (*bundle)->active_session_summary();
    REQUIRE(active.has_value());
    CHECK(active->session_id == "resume-me");
}

TEST_CASE("runtime resumed run includes history replaces system prompt and reuses session id")
{
    TempDir temp{"codeharness-runtime-resume-run-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    {
        std::ofstream file{(*bundle)->cwd() / "AGENTS.md", std::ios::binary};
        file << "fresh runtime project instructions";
    }

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "continued";
    snapshot.cwd = (*bundle)->cwd();
    snapshot.model = "echo";
    snapshot.summary = "first prompt";
    snapshot.created_at = 42.0;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::System, "stale system"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "first prompt"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::Assistant, "first answer"));
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());
    REQUIRE((*bundle)->resume_session("continued").has_value());

    auto request = (*bundle)->build_run_request("second prompt", 3);
    REQUIRE(request.has_value());
    REQUIRE(request->initial_messages.has_value());
    REQUIRE(request->initial_messages->size() == 3);
    CHECK(codeharness::collect_text(request->initial_messages->front()) == "stale system");
    REQUIRE(request->system_prompt.has_value());
    CHECK(request->system_prompt->find("fresh runtime project instructions") != std::string::npos);

    auto result = (*bundle)->run_prompt("second prompt", 3, {});
    REQUIRE(result.has_value());
    REQUIRE(result->messages.size() >= 5);
    CHECK(result->messages.front().role == codeharness::Role::System);
    CHECK(codeharness::collect_text(result->messages.front()).find("fresh runtime project instructions") !=
          std::string::npos);
    CHECK(result->output_text == "second prompt");

    auto saved = (*bundle)->sessions().load_by_id("continued");
    REQUIRE(saved);
    REQUIRE(saved->has_value());
    CHECK((*saved)->session_id == "continued");
    CHECK((*saved)->message_count == static_cast<int>(result->messages.size()));
    CHECK((*saved)->created_at == doctest::Approx(42.0));
    CHECK((*saved)->summary == "first prompt");
}

TEST_CASE("runtime resume missing session returns not found")
{
    TempDir temp{"codeharness-runtime-resume-missing-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto resumed = (*bundle)->resume_session("missing");
    REQUIRE(!resumed.has_value());
    CHECK(resumed.error().kind == codeharness::ErrorKind::NotFound);
}

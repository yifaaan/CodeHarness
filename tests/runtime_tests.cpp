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
            .permission = codeharness::PermissionSettings{.mode = mode},
            .load_default_user_plugins = false,
        });
}

auto contains_tool(const codeharness::ToolRegistry& tools, std::string_view name) -> bool
{
    const auto names = tools.names();
    return std::ranges::find(names, std::string{name}) != names.end();
}

auto make_write_bundle(TempDir& temp, codeharness::HookRegistry hooks)
    -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "write-repo";
    std::filesystem::create_directories(repo);

    codeharness::SkillRegistryLoadResult loaded_skills;
    auto memory_store = codeharness::memory::MemoryStore{temp.path / "memory"};
    auto coordinator_runtime = std::make_unique<codeharness::coordinator::CoordinatorRuntime>(
        temp.path / "tasks",
        temp.path / "teams",
        temp.path / "mailbox");
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());
    auto provider = std::make_unique<WriteFileRequestProvider>();

    auto session_store = codeharness::sessions::SessionStore::for_project(repo, temp.path / "sessions");
    if (!session_store)
    {
        return nonstd::make_unexpected(session_store.error());
    }

    return std::make_unique<codeharness::runtime::RuntimeBundle>(
        repo,
        codeharness::PermissionSettings{
            .mode = codeharness::PermissionMode::FullAuto,
            .allowed_tools = {"write_file"},
        },
        std::move(hooks),
        std::move(loaded_skills),
        std::move(memory_store),
        std::move(coordinator_runtime),
        std::move(tools),
        std::move(provider),
        "test-model",
        std::move(*session_store));
}

class RuntimeUsageProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(codeharness::ProviderUsage{.input_tokens = input_tokens, .output_tokens = output_tokens});
        sink(codeharness::AssistantTextDelta{"usage ok"});
        sink(codeharness::MessageFinished{});
        return {};
    }

    int input_tokens = 13;
    int output_tokens = 8;
};

auto make_usage_bundle(TempDir& temp, std::unique_ptr<RuntimeUsageProvider> provider)
    -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "usage-repo";
    std::filesystem::create_directories(repo);

    codeharness::SkillRegistryLoadResult loaded_skills;
    auto memory_store = codeharness::memory::MemoryStore{temp.path / "usage-memory"};
    auto coordinator_runtime = std::make_unique<codeharness::coordinator::CoordinatorRuntime>(
        temp.path / "usage-tasks",
        temp.path / "usage-teams",
        temp.path / "usage-mailbox");
    codeharness::ToolRegistry tools;

    auto session_store = codeharness::sessions::SessionStore::for_project(repo, temp.path / "usage-sessions");
    if (!session_store)
    {
        return nonstd::make_unexpected(session_store.error());
    }

    return std::make_unique<codeharness::runtime::RuntimeBundle>(
        repo,
        codeharness::PermissionSettings{.mode = codeharness::PermissionMode::Default},
        codeharness::HookRegistry{},
        std::move(loaded_skills),
        std::move(memory_store),
        std::move(coordinator_runtime),
        std::move(tools),
        std::move(provider),
        "usage-model",
        std::move(*session_store));
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

TEST_CASE("runtime plan mode commands toggle and report permission mode")
{
    TempDir temp{"codeharness-runtime-plan-mode-command-test"};

    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::Default);

    auto entered = (*bundle)->run_prompt("/plan", 3, {});
    REQUIRE(entered.has_value());
    CHECK(entered->output_text.find("Entered plan mode") != std::string::npos);
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::Plan);

    auto mode = (*bundle)->run_prompt("/mode", 3, {});
    REQUIRE(mode.has_value());
    CHECK(mode->output_text == "Current permission mode: plan");

    auto request = (*bundle)->build_run_request("inspect only", 3);
    REQUIRE(request.has_value());
    REQUIRE(request->system_prompt.has_value());
    CHECK(request->system_prompt->find("Plan: treat the session as read-only planning") != std::string::npos);

    auto exited = (*bundle)->run_prompt("/act", 3, {});
    REQUIRE(exited.has_value());
    CHECK(exited->output_text.find("Default mode") != std::string::npos);
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::Default);
}

TEST_CASE("runtime set_permission_mode updates future run request permission guidance")
{
    TempDir temp{"codeharness-runtime-set-plan-mode-test"};

    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    (*bundle)->set_permission_mode(codeharness::PermissionMode::Plan);
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::Plan);

    auto request = (*bundle)->build_run_request("plan this", 5);
    REQUIRE(request.has_value());
    REQUIRE(request->system_prompt.has_value());
    CHECK(request->system_prompt->find("Plan: treat the session as read-only planning") != std::string::npos);
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

TEST_CASE("runtime executes configured command hook through engine wiring")
{
    TempDir temp{"codeharness-runtime-hook-exec-test"};
    const auto marker = (temp.path / "hook-marker.txt").string();

#if defined(_WIN32)
    const auto argv = nlohmann::json::array(
        {"powershell.exe", "-NoProfile", "-Command", std::string{"Set-Content -LiteralPath '"} + marker + "' -Value ran"});
#else
    const auto argv = nlohmann::json::array({"/bin/sh", "-c", std::string{"printf 'ran\\n' > \""} + marker + "\""});
#endif

    codeharness::HookRegistry hooks;
    hooks.add(codeharness::HookDefinition{
        .event = codeharness::HookEvent::PreToolUse,
        .type = codeharness::HookType::Command,
        .matcher = std::string{"write_file"},
        .block_on_failure = true,
        .config = nlohmann::json{{"argv", argv}},
    });

    auto bundle = make_write_bundle(temp, std::move(hooks));
    REQUIRE(bundle.has_value());

    auto result = (*bundle)->run_prompt("write output.txt", 3, {});

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("Created") != std::string::npos);
    CHECK(std::filesystem::exists(temp.path / "hook-marker.txt"));
}

TEST_CASE("runtime mode toggles keep hook executor wired")
{
    TempDir temp{"codeharness-runtime-hook-after-mode-toggle-test"};
    const auto marker = (temp.path / "hook-after-mode.txt").string();

#if defined(_WIN32)
    const auto argv = nlohmann::json::array(
        {"powershell.exe", "-NoProfile", "-Command", std::string{"Set-Content -LiteralPath '"} + marker + "' -Value ran"});
#else
    const auto argv = nlohmann::json::array({"/bin/sh", "-c", std::string{"printf 'ran\\n' > \""} + marker + "\""});
#endif

    codeharness::HookRegistry hooks;
    hooks.add(codeharness::HookDefinition{
        .event = codeharness::HookEvent::PreToolUse,
        .type = codeharness::HookType::Command,
        .matcher = std::string{"write_file"},
        .block_on_failure = true,
        .config = nlohmann::json{{"argv", argv}},
    });

    auto bundle = make_write_bundle(temp, std::move(hooks));
    REQUIRE(bundle.has_value());
    REQUIRE((*bundle)->run_prompt("/plan", 3, {}).has_value());
    REQUIRE((*bundle)->run_prompt("/act", 3, {}).has_value());

    auto result = (*bundle)->run_prompt("write output.txt", 3, {});

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("Created") != std::string::npos);
    CHECK(std::filesystem::exists(temp.path / "hook-after-mode.txt"));
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

TEST_CASE("runtime saves provider usage into latest session")
{
    TempDir temp{"codeharness-runtime-usage-save-test"};
    auto bundle = make_usage_bundle(temp, std::make_unique<RuntimeUsageProvider>());
    REQUIRE(bundle.has_value());

    auto result = (*bundle)->run_prompt("track usage", 1, {});
    REQUIRE(result.has_value());
    CHECK(result->usage.input_tokens == 13);
    CHECK(result->usage.output_tokens == 8);
    CHECK(result->usage.total_tokens == 21);

    const auto latest_usage = (*bundle)->latest_usage();
    CHECK(latest_usage.input_tokens == 13);
    CHECK(latest_usage.output_tokens == 8);
    CHECK(latest_usage.total_tokens == 21);

    auto loaded = (*bundle)->sessions().load_latest();
    REQUIRE(loaded);
    REQUIRE(loaded->has_value());
    CHECK((*loaded)->usage.input_tokens == 13);
    CHECK((*loaded)->usage.output_tokens == 8);
    CHECK((*loaded)->usage.total_tokens == 21);
}

TEST_CASE("runtime resume keeps saved usage until next run overwrites it")
{
    TempDir temp{"codeharness-runtime-usage-resume-test"};
    auto provider = std::make_unique<RuntimeUsageProvider>();
    provider->input_tokens = 5;
    provider->output_tokens = 4;
    auto bundle = make_usage_bundle(temp, std::move(provider));
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "usage-resume";
    snapshot.cwd = (*bundle)->cwd();
    snapshot.model = "usage-model";
    snapshot.summary = "saved";
    snapshot.created_at = 10.0;
    snapshot.usage = codeharness::sessions::UsageSnapshot{.input_tokens = 100, .output_tokens = 50, .total_tokens = 150};
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "saved"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::Assistant, "answer"));
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    REQUIRE((*bundle)->resume_session("usage-resume").has_value());
    auto resumed_usage = (*bundle)->latest_usage();
    CHECK(resumed_usage.input_tokens == 100);
    CHECK(resumed_usage.output_tokens == 50);
    CHECK(resumed_usage.total_tokens == 150);

    auto result = (*bundle)->run_prompt("new usage", 1, {});
    REQUIRE(result.has_value());
    auto updated_usage = (*bundle)->latest_usage();
    CHECK(updated_usage.input_tokens == 5);
    CHECK(updated_usage.output_tokens == 4);
    CHECK(updated_usage.total_tokens == 9);
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

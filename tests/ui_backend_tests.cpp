#include "codeharness/ui_backend/ui_backend.h"

#include "test_support.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>
#include <string_view>
#include <thread>

namespace
{

constexpr std::string_view kBackendPrefix = "OHJSON:";

auto make_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "repo";
    const auto memory_root = temp.path / "memory-root";
    std::filesystem::create_directories(repo);

    return codeharness::runtime::create_runtime_bundle(
        codeharness::runtime::RuntimeBundleOptions{
            .cwd = repo,
            .memory_root = memory_root,
            .load_default_user_plugins = false,
        });
}

auto make_write_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
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
        codeharness::PermissionSettings{.mode = codeharness::PermissionMode::Default},
        codeharness::HookRegistry{},
        std::move(loaded_skills),
        std::move(memory_store),
        std::move(coordinator_runtime),
        std::move(tools),
        std::move(provider),
        "test-model",
        std::move(*session_store));
}

class BackendUsageProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(codeharness::ProviderUsage{.input_tokens = 12, .output_tokens = 6, .total_tokens = 18});
        sink(codeharness::AssistantTextDelta{"usage"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class BackendAskUserProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        if (!messages.empty() && messages.back().role == codeharness::Role::Tool)
        {
            for (const auto& block : messages.back().content)
            {
                if (const auto* result = std::get_if<codeharness::ToolResultBlock>(&block))
                {
                    sink(codeharness::AssistantTextDelta{result->content});
                    sink(codeharness::MessageFinished{});
                    return {};
                }
            }
            sink(codeharness::MessageFinished{});
            return {};
        }

        sink(codeharness::ToolUseStarted{.id = "tool-use-ask", .name = "ask_user"});
        sink(codeharness::ToolUseInputDelta{
            .id = "tool-use-ask",
            .input_json_delta = R"({"question":"Which file?","reason":"Need target"})",
        });
        sink(codeharness::ToolUseFinished{.id = "tool-use-ask"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class BackendRepeatingWriteProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        if (!messages.empty() && messages.back().role == codeharness::Role::Tool)
        {
            sink(codeharness::AssistantTextDelta{"done"});
            sink(codeharness::MessageFinished{});
            return {};
        }

        const auto path = paths.at(std::min(prompt_index, paths.size() - 1));
        ++prompt_index;
        const auto id = "tool-use-" + std::to_string(prompt_index);
        sink(codeharness::ToolUseStarted{.id = id, .name = "write_file"});
        sink(codeharness::ToolUseInputDelta{
            .id = id,
            .input_json_delta = R"({"path":")" + path + R"(","content":"hello"})",
        });
        sink(codeharness::ToolUseFinished{.id = id});
        sink(codeharness::MessageFinished{});
        return {};
    }

    std::vector<std::string> paths{"output.txt"};
    std::size_t prompt_index = 0;
};

auto make_repeating_write_bundle(TempDir& temp, std::unique_ptr<BackendRepeatingWriteProvider> provider)
    -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "remember-write-repo";
    std::filesystem::create_directories(repo);

    codeharness::SkillRegistryLoadResult loaded_skills;
    auto memory_store = codeharness::memory::MemoryStore{temp.path / "remember-memory"};
    auto coordinator_runtime = std::make_unique<codeharness::coordinator::CoordinatorRuntime>(
        temp.path / "remember-tasks",
        temp.path / "remember-teams",
        temp.path / "remember-mailbox");
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    auto session_store = codeharness::sessions::SessionStore::for_project(repo, temp.path / "remember-sessions");
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
        "test-model",
        std::move(*session_store));
}

auto make_usage_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
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
    auto provider = std::make_unique<BackendUsageProvider>();

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
        "test-model",
        std::move(*session_store));
}

auto make_ask_user_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "ask-user-repo";
    std::filesystem::create_directories(repo);

    codeharness::SkillRegistryLoadResult loaded_skills;
    auto memory_store = codeharness::memory::MemoryStore{temp.path / "ask-user-memory"};
    auto coordinator_runtime = std::make_unique<codeharness::coordinator::CoordinatorRuntime>(
        temp.path / "ask-user-tasks",
        temp.path / "ask-user-teams",
        temp.path / "ask-user-mailbox");
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::AskUserTool>());
    auto provider = std::make_unique<BackendAskUserProvider>();

    auto session_store = codeharness::sessions::SessionStore::for_project(repo, temp.path / "ask-user-sessions");
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
        "test-model",
        std::move(*session_store));
}

auto parse_backend_output(std::string_view output) -> std::vector<nlohmann::json>
{
    std::vector<nlohmann::json> events;
    std::istringstream stream{std::string{output}};
    std::string line;
    while (std::getline(stream, line))
    {
        REQUIRE(line.starts_with(kBackendPrefix));
        events.push_back(nlohmann::json::parse(line.substr(kBackendPrefix.size())));
    }

    return events;
}

auto run_backend_host(codeharness::runtime::RuntimeBundle& runtime, std::string input, int max_turns = 3)
    -> std::vector<nlohmann::json>
{
    std::istringstream input_stream{std::move(input)};
    std::ostringstream output_stream;
    codeharness::ui_backend::BackendHost host{runtime, input_stream, output_stream, max_turns};

    auto result = host.run();

    REQUIRE(result.has_value());
    return parse_backend_output(output_stream.str());
}

} // namespace

TEST_CASE("ui backend parses frontend requests")
{
    auto request = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"submit_line","line":"hello"})");

    REQUIRE(request.has_value());
    CHECK(request->type == "submit_line");
    REQUIRE(request->line.has_value());
    CHECK(*request->line == "hello");

    auto shutdown = codeharness::ui_backend::parse_frontend_request(R"({"type":"shutdown"})");
    REQUIRE(shutdown.has_value());
    CHECK(shutdown->type == "shutdown");
    CHECK(!shutdown->line.has_value());

    auto permission = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"permission_response","request_id":"perm-1","allowed":true})");
    REQUIRE(permission.has_value());
    CHECK(permission->type == "permission_response");
    REQUIRE(permission->request_id.has_value());
    CHECK(*permission->request_id == "perm-1");
    REQUIRE(permission->allowed.has_value());
    CHECK(*permission->allowed);
    CHECK(!permission->remember_session.has_value());

    auto remembered = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"permission_response","request_id":"perm-1","allowed":true,"remember_session":true})");
    REQUIRE(remembered.has_value());
    REQUIRE(remembered->remember_session.has_value());
    CHECK(*remembered->remember_session);
}

TEST_CASE("ui backend rejects malformed frontend requests")
{
    auto malformed = codeharness::ui_backend::parse_frontend_request("{");
    REQUIRE(!malformed.has_value());
    CHECK(malformed.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(malformed.error().message.find("failed to parse frontend request") != std::string::npos);

    auto missing_type = codeharness::ui_backend::parse_frontend_request(R"({"line":"hello"})");
    REQUIRE(!missing_type.has_value());
    CHECK(missing_type.error().message.find("requires string field: type") != std::string::npos);

    auto wrong_line_type = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"submit_line","line":42})");
    REQUIRE(!wrong_line_type.has_value());
    CHECK(wrong_line_type.error().message.find("requires string field: line") != std::string::npos);
}

TEST_CASE("ui backend formats OHJSON events")
{
    const auto formatted = codeharness::ui_backend::format_backend_event(
        codeharness::ui_backend::BackendAssistantDelta{.text = "hello"});

    CHECK(formatted.starts_with(kBackendPrefix));
    CHECK(formatted.ends_with('\n'));

    const auto event = nlohmann::json::parse(formatted.substr(kBackendPrefix.size()));
    CHECK(event.at("type") == "assistant_delta");
    CHECK(event.at("text") == "hello");

    const auto modal = codeharness::ui_backend::format_backend_event(
        codeharness::ui_backend::BackendPermissionModal{
            .id = "perm-tool-use-1",
            .tool_use_id = "tool-use-1",
            .tool_name = "write_file",
            .reason = "default mode requires confirmation for mutating tools",
            .path = "output.txt",
        });
    const auto modal_event = nlohmann::json::parse(modal.substr(kBackendPrefix.size()));
    CHECK(modal_event.at("type") == "modal_request");
    CHECK(modal_event.at("modal").at("kind") == "permission");
    CHECK(modal_event.at("modal").at("request_id") == "perm-tool-use-1");
    CHECK(modal_event.at("modal").at("tool_name") == "write_file");

    const auto select = codeharness::ui_backend::format_backend_event(
        codeharness::ui_backend::BackendSelectRequest{
            .commands =
                {
                    codeharness::ui_backend::BackendCommandEntry{
                        .name = "skills",
                        .description = "List loaded skills.",
                        .aliases = {"skill-list"},
                        .invocation = "message_only",
                    },
                },
        });
    const auto select_event = nlohmann::json::parse(select.substr(kBackendPrefix.size()));
    CHECK(select_event.at("type") == "select_request");
    CHECK(select_event.at("commands").at(0).at("name") == "skills");
    CHECK(select_event.at("commands").at(0).at("aliases").at(0) == "skill-list");
}

class InterruptingProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(codeharness::AssistantTextDelta{"partial"});
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

auto make_interrupting_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "interrupt-repo";
    std::filesystem::create_directories(repo);

    codeharness::SkillRegistryLoadResult loaded_skills;
    auto memory_store = codeharness::memory::MemoryStore{temp.path / "memory"};
    auto coordinator_runtime = std::make_unique<codeharness::coordinator::CoordinatorRuntime>(
        temp.path / "tasks",
        temp.path / "teams",
        temp.path / "mailbox");
    codeharness::ToolRegistry tools;
    auto provider = std::make_unique<InterruptingProvider>();

    auto session_store = codeharness::sessions::SessionStore::for_project(repo, temp.path / "sessions");
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
        "test-model",
        std::move(*session_store));
}

TEST_CASE("ui backend emits errors and shutdown without throwing")
{
    TempDir temp{"codeharness-ui-backend-errors-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        "{\n"
        R"({"type":"unknown"})"
        "\n"
        R"({"type":"submit_line","line":""})"
        "\n"
        R"({"type":"shutdown"})"
        "\n"
        R"({"type":"submit_line","line":"after shutdown"})"
        "\n");

    REQUIRE(events.size() == 5);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message").get<std::string>().find("failed to parse frontend request") != std::string::npos);
    CHECK(events.at(2).at("type") == "error");
    CHECK(events.at(2).at("message") == "unsupported frontend request type: unknown");
    CHECK(events.at(3).at("type") == "error");
    CHECK(events.at(3).at("message") == "submit_line requires non-empty line");
    CHECK(events.at(4).at("type") == "shutdown");
}

TEST_CASE("ui backend emits usage event after prompt completion")
{
    TempDir temp{"codeharness-ui-backend-usage-test"};
    auto bundle = make_usage_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"hello"})"
        "\n");

    bool saw_usage = false;
    for (const auto& event : events)
    {
        if (event.at("type") != "usage")
        {
            continue;
        }
        saw_usage = true;
        CHECK(event.at("input_tokens") == 12);
        CHECK(event.at("output_tokens") == 6);
        CHECK(event.at("total_tokens") == 18);
    }
    REQUIRE(events.size() >= 4);
    CHECK(saw_usage);
    CHECK(events.back().at("type") == "line_complete");
}

TEST_CASE("ui backend rejects permission_response without pending request")
{
    TempDir temp{"codeharness-ui-backend-permission-no-pending-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"permission_response","request_id":"perm-1","allowed":true})"
        "\n");

    REQUIRE(events.size() == 2);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message") == "permission_response has no pending permission request");
}

TEST_CASE("ui backend interrupt without active run emits error")
{
    TempDir temp{"codeharness-ui-backend-interrupt-idle-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"interrupt"})" "\n");

    REQUIRE(events.size() == 2);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message") == "interrupt has no active run");
}

TEST_CASE("ui backend interrupt cancels active run")
{
    TempDir temp{"codeharness-ui-backend-interrupt-active-test"};
    auto bundle = make_interrupting_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"long"})"
        "\n"
        R"({"type":"interrupt"})"
        "\n");

    bool saw_interrupted = false;
    bool saw_complete = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "error" && event.at("message") == "interrupted")
        {
            saw_interrupted = true;
        }
        if (event.at("type") == "line_complete")
        {
            saw_complete = true;
        }
    }

    CHECK(saw_interrupted);
    CHECK(saw_complete);
}

TEST_CASE("ui backend rejects non-control requests during active run")
{
    TempDir temp{"codeharness-ui-backend-busy-test"};
    auto bundle = make_interrupting_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"long"})"
        "\n"
        R"({"type":"select_command"})"
        "\n"
        R"({"type":"interrupt"})"
        "\n");

    bool saw_busy = false;
    bool saw_complete = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "error" && event.at("message") == "run already in progress")
        {
            saw_busy = true;
        }
        if (event.at("type") == "line_complete")
        {
            saw_complete = true;
        }
    }

    CHECK(saw_busy);
    CHECK(saw_complete);
}

TEST_CASE("ui backend permission response approves pending tool")
{
    TempDir temp{"codeharness-ui-backend-permission-approve-test"};
    auto bundle = make_write_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"write output"})"
        "\n"
        R"({"type":"permission_response","request_id":"perm-tool-use-1","allowed":true})"
        "\n");

    REQUIRE(events.size() >= 5);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "tool_started");

    bool saw_modal = false;
    bool saw_success = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "modal_request" &&
            event.at("modal").at("request_id") == "perm-tool-use-1")
        {
            saw_modal = true;
        }
        if (event.at("type") == "tool_result" &&
            event.at("content").get<std::string>().find("Created") != std::string::npos)
        {
            saw_success = true;
        }
    }
    CHECK(saw_modal);
    CHECK(saw_success);
    CHECK(std::filesystem::exists(temp.path / "write-repo" / "output.txt"));
}

TEST_CASE("ui backend permission response can remember same target for session")
{
    TempDir temp{"codeharness-ui-backend-permission-remember-test"};
    auto provider = std::make_unique<BackendRepeatingWriteProvider>();
    provider->paths = {"output.txt", "output.txt", "other.txt"};
    auto bundle = make_repeating_write_bundle(temp, std::move(provider));
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"first"})"
        "\n"
        R"({"type":"permission_response","request_id":"perm-tool-use-1","allowed":true,"remember_session":true})"
        "\n"
        R"({"type":"submit_line","line":"second"})"
        "\n"
        R"({"type":"submit_line","line":"third"})"
        "\n"
        R"({"type":"permission_response","request_id":"perm-tool-use-3","allowed":true})"
        "\n");

    int modal_count = 0;
    bool saw_first_modal = false;
    bool saw_third_modal = false;
    for (const auto& event : events)
    {
        if (event.at("type") != "modal_request")
        {
            continue;
        }
        ++modal_count;
        const auto request_id = event.at("modal").at("request_id").get<std::string>();
        saw_first_modal = saw_first_modal || request_id == "perm-tool-use-1";
        saw_third_modal = saw_third_modal || request_id == "perm-tool-use-3";
    }

    CHECK(modal_count == 2);
    CHECK(saw_first_modal);
    CHECK(saw_third_modal);
}

TEST_CASE("ui backend permission response denies pending tool")
{
    TempDir temp{"codeharness-ui-backend-permission-deny-test"};
    auto bundle = make_write_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"write output"})"
        "\n"
        R"({"type":"permission_response","request_id":"perm-tool-use-1","allowed":false})"
        "\n");

    REQUIRE(events.size() >= 5);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "tool_started");

    bool saw_modal = false;
    bool saw_denial = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "modal_request")
        {
            saw_modal = true;
        }
        if (event.at("type") == "tool_result" &&
            event.at("content").get<std::string>().find("permission denied: user denied permission") !=
                std::string::npos)
        {
            saw_denial = true;
            CHECK(event.at("is_error") == true);
        }
    }
    CHECK(saw_modal);
    CHECK(saw_denial);
    CHECK(!std::filesystem::exists(temp.path / "write-repo" / "output.txt"));
}

TEST_CASE("ui backend ask_user response continues run")
{
    TempDir temp{"codeharness-ui-backend-ask-user-test"};
    auto bundle = make_ask_user_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"need input"})"
        "\n"
        R"({"type":"user_question_response","request_id":"ask-tool-use-ask","answer":"src/main.cpp"})"
        "\n");

    bool saw_modal = false;
    bool saw_answer = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "modal_request" &&
            event.at("modal").at("kind") == "ask_user")
        {
            saw_modal = true;
            CHECK(event.at("modal").at("request_id") == "ask-tool-use-ask");
            CHECK(event.at("modal").at("question") == "Which file?");
        }
        if (event.at("type") == "assistant_delta" &&
            event.at("text").get<std::string>().find("src/main.cpp") != std::string::npos)
        {
            saw_answer = true;
        }
    }

    CHECK(saw_modal);
    CHECK(saw_answer);
    CHECK(events.back().at("type") == "line_complete");
}

TEST_CASE("ui backend ask_user response id mismatch does not unblock")
{
    TempDir temp{"codeharness-ui-backend-ask-user-mismatch-test"};
    auto bundle = make_ask_user_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"need input"})"
        "\n"
        R"({"type":"user_question_response","request_id":"wrong","answer":"nope"})"
        "\n"
        R"({"type":"user_question_response","request_id":"ask-tool-use-ask","answer":"ok"})"
        "\n");

    bool saw_mismatch = false;
    bool saw_answer = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "error" && event.at("message") == "user question response request_id mismatch")
        {
            saw_mismatch = true;
        }
        if (event.at("type") == "assistant_delta" &&
            event.at("text").get<std::string>().find("ok") != std::string::npos)
        {
            saw_answer = true;
        }
    }

    CHECK(saw_mismatch);
    CHECK(saw_answer);
}

TEST_CASE("ui backend interrupt unblocks pending ask_user")
{
    TempDir temp{"codeharness-ui-backend-ask-user-interrupt-test"};
    auto bundle = make_ask_user_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"need input"})"
        "\n"
        R"({"type":"user_question_response","request_id":"wrong","answer":"nope"})"
        "\n"
        R"({"type":"interrupt"})"
        "\n");

    bool saw_modal = false;
    bool saw_mismatch = false;
    bool saw_complete = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "modal_request" &&
            event.at("modal").at("kind") == "ask_user")
        {
            saw_modal = true;
        }
        if (event.at("type") == "error" && event.at("message") == "user question response request_id mismatch")
        {
            saw_mismatch = true;
        }
        if (event.at("type") == "line_complete")
        {
            saw_complete = true;
        }
    }

    CHECK(saw_modal);
    CHECK(saw_mismatch);
    CHECK(saw_complete);
}

TEST_CASE("ui backend permission response id mismatch does not unblock")
{
    TempDir temp{"codeharness-ui-backend-permission-mismatch-test"};
    auto bundle = make_write_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"write output"})"
        "\n"
        R"({"type":"permission_response","request_id":"wrong","allowed":true})"
        "\n"
        R"({"type":"permission_response","request_id":"perm-tool-use-1","allowed":false})"
        "\n");

    bool saw_mismatch = false;
    bool saw_denial = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "error" && event.at("message") == "permission response request_id mismatch")
        {
            saw_mismatch = true;
        }
        if (event.at("type") == "tool_result" &&
            event.at("content").get<std::string>().find("permission denied") != std::string::npos)
        {
            saw_denial = true;
        }
    }

    CHECK(saw_mismatch);
    CHECK(saw_denial);
    CHECK(!std::filesystem::exists(temp.path / "write-repo" / "output.txt"));
}

TEST_CASE("ui backend interrupt unblocks pending permission")
{
    TempDir temp{"codeharness-ui-backend-permission-interrupt-test"};
    auto bundle = make_write_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"write output"})"
        "\n"
        R"({"type":"interrupt"})"
        "\n");

    bool saw_interrupted = false;
    bool saw_complete = false;
    for (const auto& event : events)
    {
        if (event.at("type") == "error" && event.at("message") == "interrupted")
        {
            saw_interrupted = true;
        }
        if (event.at("type") == "line_complete")
        {
            saw_complete = true;
        }
    }

    CHECK(saw_interrupted);
    CHECK(saw_complete);
    CHECK(!std::filesystem::exists(temp.path / "write-repo" / "output.txt"));
}

TEST_CASE("ui backend submit_line streams assistant delta and line complete")
{
    TempDir temp{"codeharness-ui-backend-submit-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"hello backend"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text") == "hello backend");
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend message-only slash command emits output and completes")
{
    TempDir temp{"codeharness-ui-backend-slash-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/skills"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Available skills:") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend fullauto command changes permission mode")
{
    TempDir temp{"codeharness-ui-backend-fullauto-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"/fullauto"})"
        "\n"
        R"({"type":"submit_line","line":"/mode"})"
        "\n");

    REQUIRE(events.size() == 5);
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Full-auto mode") != std::string::npos);
    CHECK(events.at(3).at("type") == "assistant_delta");
    CHECK(events.at(3).at("text") == "Current permission mode: full_auto");
    CHECK((*bundle)->permission_mode() == codeharness::PermissionMode::FullAuto);
}

TEST_CASE("ui backend select_command emits available commands")
{
    TempDir temp{"codeharness-ui-backend-select-command-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"select_command","query":"skill"})" "\n");

    REQUIRE(events.size() == 2);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "select_request");

    bool saw_skills = false;
    for (const auto& command : events.at(1).at("commands"))
    {
        if (command.at("name") == "skills")
        {
            saw_skills = true;
            CHECK(command.at("description").get<std::string>().find("skills") != std::string::npos);
            CHECK(command.at("invocation") == "message_only");
        }
    }
    CHECK(saw_skills);
}

TEST_CASE("ui backend apply_select_command executes selected slash command")
{
    TempDir temp{"codeharness-ui-backend-apply-select-command-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"apply_select_command","command":"skills"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Available skills:") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend apply_select_command rejects missing or unknown command")
{
    TempDir temp{"codeharness-ui-backend-apply-select-command-error-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"apply_select_command"})"
        "\n"
        R"({"type":"apply_select_command","command":"missing"})"
        "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message") == "apply_select_command requires command");
    CHECK(events.at(2).at("type") == "error");
    CHECK(events.at(2).at("message") == "unknown command: /missing");
}

TEST_CASE("ui backend sessions command lists saved sessions")
{
    TempDir temp{"codeharness-ui-backend-sessions-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "listed";
    snapshot.summary = "listed summary";
    snapshot.model = "echo";
    snapshot.message_count = 2;
    snapshot.created_at = 10.0;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "listed summary"));
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/sessions"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    const auto text = events.at(1).at("text").get<std::string>();
    CHECK(text.find("Sessions:") != std::string::npos);
    CHECK(text.find("listed") != std::string::npos);
    CHECK(text.find("listed summary") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend resume command loads session without invoking provider")
{
    TempDir temp{"codeharness-ui-backend-resume-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "resume-backend";
    snapshot.summary = "old work";
    snapshot.message_count = 1;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "old work"));
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/resume latest"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Resumed session resume-backend: old work") !=
          std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");

    auto active = (*bundle)->active_session_summary();
    REQUIRE(active.has_value());
    CHECK(active->session_id == "resume-backend");
}

TEST_CASE("ui backend prompt after resume continues and saves same session")
{
    TempDir temp{"codeharness-ui-backend-resume-continue-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "same-backend";
    snapshot.summary = "first backend";
    snapshot.created_at = 25.0;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "first backend"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::Assistant, "first answer"));
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"/resume same-backend"})"
        "\n"
        R"({"type":"submit_line","line":"second backend"})"
        "\n");

    REQUIRE(events.size() == 5);
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Resumed session same-backend") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
    CHECK(events.at(3).at("type") == "assistant_delta");
    CHECK(events.at(3).at("text") == "second backend");
    CHECK(events.at(4).at("type") == "line_complete");

    auto saved = (*bundle)->sessions().load_by_id("same-backend");
    REQUIRE(saved);
    REQUIRE(saved->has_value());
    CHECK((*saved)->session_id == "same-backend");
    CHECK((*saved)->summary == "first backend");
    CHECK((*saved)->created_at == doctest::Approx(25.0));
    CHECK((*saved)->message_count >= 4);
}

TEST_CASE("ui backend resume missing session emits error")
{
    TempDir temp{"codeharness-ui-backend-resume-missing-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/resume missing"})" "\n");

    REQUIRE(events.size() == 2);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message") == "session not found: missing");
}

TEST_CASE("ui backend processes multiple frontend requests in order")
{
    TempDir temp{"codeharness-ui-backend-multiple-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"first"})"
        "\n"
        R"({"type":"submit_line","line":"second"})"
        "\n"
        R"({"type":"shutdown"})"
        "\n");

    REQUIRE(events.size() >= 4);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text") == "first");
    CHECK(events.at(2).at("type") == "line_complete");
    CHECK(events.back().at("type") == "shutdown");
}

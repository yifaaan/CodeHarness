#include "test_support.h"

#include "codeharness/coordinator/agent_definition.h"
#include "codeharness/coordinator/spawn_config.h"
#include "codeharness/coordinator/subprocess_backend.h"
#include "codeharness/coordinator/task_notification.h"

using codeharness::coordinator::AgentDefinition;
using codeharness::coordinator::AgentDefinitionLoadOptions;
using codeharness::coordinator::AgentDefinitionRegistry;
using codeharness::coordinator::apply_agent_definition;
using codeharness::coordinator::discover_project_agent_dirs;
using codeharness::coordinator::load_agent_definition_file;
using codeharness::coordinator::load_agent_definition_registry;
using codeharness::coordinator::load_agent_definitions;
using codeharness::coordinator::load_agent_definitions_from_dirs;
using codeharness::coordinator::make_task_notification;
using codeharness::coordinator::make_task_result_message;
using codeharness::coordinator::parse_agent_definition_markdown;
using codeharness::coordinator::resolve_spawn_config;
using codeharness::coordinator::SubprocessBackend;
using codeharness::coordinator::task_notification_to_xml;
using codeharness::coordinator::TeammateSpawnConfig;

namespace
{

auto write_text(const std::filesystem::path& path, std::string_view content) -> void
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.good());
    file << content;
}

auto codeharness_version_argv() -> std::vector<std::string>
{
    return {std::string{CODEHARNESS_EXE}, "--version"};
}

} // namespace

TEST_CASE("agent definition parser reads frontmatter and system prompt body")
{
    auto agent = parse_agent_definition_markdown(
        "reviewer",
        "---\n"
        "name: cpp-reviewer\n"
        "description: Review C++ changes carefully\n"
        "tools: [read_file, grep, glob]\n"
        "disallowed_tools:\n"
        "  - bash\n"
        "model: claude-sonnet-4-6\n"
        "effort: high\n"
        "permission_mode: plan\n"
        "max_turns: 7\n"
        "skills: [review, test]\n"
        "mcp_servers: [filesystem]\n"
        "---\n"
        "You are a careful C++ reviewer.\n"
        "Focus on correctness and race conditions.\n",
        "user");

    CHECK(agent.name == "cpp-reviewer");
    CHECK(agent.description == "Review C++ changes carefully");
    CHECK(agent.system_prompt.find("careful C++ reviewer") != std::string::npos);
    CHECK(agent.tools == std::vector<std::string>{"read_file", "grep", "glob"});
    CHECK(agent.disallowed_tools == std::vector<std::string>{"bash"});
    REQUIRE(agent.model.has_value());
    CHECK(*agent.model == "claude-sonnet-4-6");
    REQUIRE(agent.effort.has_value());
    CHECK(*agent.effort == "high");
    REQUIRE(agent.permission_mode.has_value());
    CHECK(*agent.permission_mode == "plan");
    REQUIRE(agent.max_turns.has_value());
    CHECK(*agent.max_turns == 7);
    CHECK(agent.skills == std::vector<std::string>{"review", "test"});
    CHECK(agent.mcp_servers == std::vector<std::string>{"filesystem"});
    CHECK(agent.source == "user");
}

TEST_CASE("agent definition parser supports camelCase aliases")
{
    auto agent = parse_agent_definition_markdown(
        "worker",
        "---\n"
        "description: Worker with upstream-style field names\n"
        "disallowedTools: [write_file]\n"
        "permissionMode: dontAsk\n"
        "maxTurns: 3\n"
        "mcpServers: [git]\n"
        "---\n"
        "Worker prompt.\n");

    CHECK(agent.name == "worker");
    CHECK(agent.disallowed_tools == std::vector<std::string>{"write_file"});
    REQUIRE(agent.permission_mode.has_value());
    CHECK(*agent.permission_mode == "dontAsk");
    REQUIRE(agent.max_turns.has_value());
    CHECK(*agent.max_turns == 3);
    CHECK(agent.mcp_servers == std::vector<std::string>{"git"});
}

TEST_CASE("agent definition parser falls back to heading and first body line")
{
    auto agent = parse_agent_definition_markdown(
        "default-worker",
        "# Researcher\n\n"
        "Search broadly before answering.\n"
        "Use read-only tools.\n");

    CHECK(agent.name == "Researcher");
    CHECK(agent.description == "Search broadly before answering.");
    CHECK(agent.system_prompt.find("Use read-only tools") != std::string::npos);
}

TEST_CASE("agent definition file loader records path and base dir")
{
    TempDir temp{"codeharness-agent-file-test"};
    const auto path = temp.path / "agents" / "reviewer.md";
    write_text(path,
               "---\n"
               "description: Review files\n"
               "---\n"
               "Review prompt.\n");

    auto agent = load_agent_definition_file(path, "project");

    REQUIRE(agent.has_value());
    CHECK(agent->name == "reviewer");
    CHECK(agent->description == "Review files");
    CHECK(agent->path == path);
    CHECK(agent->base_dir == path.parent_path());
    CHECK(agent->source == "project");
}

TEST_CASE("agent definition directory loader sorts and skips missing roots")
{
    TempDir temp{"codeharness-agent-dir-test"};
    const auto agents_dir = temp.path / "agents";
    write_text(agents_dir / "zeta.md", "---\ndescription: Z\n---\nZ prompt\n");
    write_text(agents_dir / "alpha.md", "---\ndescription: A\n---\nA prompt\n");

    const std::vector<std::filesystem::path> dirs{temp.path / "missing", agents_dir};
    auto agents = load_agent_definitions_from_dirs(dirs, "user");

    REQUIRE(agents.has_value());
    REQUIRE(agents->size() == 2);
    CHECK((*agents)[0].name == "alpha");
    CHECK((*agents)[1].name == "zeta");
}

TEST_CASE("project agent discovery returns parent before child")
{
    TempDir temp{"codeharness-project-agent-discovery-test"};
    const auto repo = temp.path / "repo";
    const auto child = repo / "src" / "feature";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(repo / ".agents" / "agents");
    std::filesystem::create_directories(child / ".agents" / "agents");

    const std::vector<std::filesystem::path> relative_dirs{".agents/agents"};
    auto dirs = discover_project_agent_dirs(child, relative_dirs);

    REQUIRE(dirs.has_value());
    REQUIRE(dirs->size() == 2);
    CHECK((*dirs)[0] == repo / ".agents" / "agents");
    CHECK((*dirs)[1] == child / ".agents" / "agents");
}

TEST_CASE("load_agent_definitions composes user extra and project directories")
{
    TempDir temp{"codeharness-load-agent-definitions-test"};
    const auto repo = temp.path / "repo";
    const auto project_agents = repo / ".agents" / "agents";
    const auto user_agents = temp.path / "user-agents";
    const auto extra_agents = temp.path / "extra-agents";
    std::filesystem::create_directories(repo / ".git");

    write_text(user_agents / "user.md", "---\ndescription: User agent\n---\nUser prompt\n");
    write_text(extra_agents / "extra.md", "---\ndescription: Extra agent\n---\nExtra prompt\n");
    write_text(project_agents / "project.md", "---\ndescription: Project agent\n---\nProject prompt\n");

    AgentDefinitionLoadOptions options;
    options.load_default_user_agents = false;
    options.user_agent_dirs = {user_agents};
    options.extra_agent_dirs = {extra_agents};
    options.project_agent_dirs = {".agents/agents"};

    auto agents = load_agent_definitions(repo, std::move(options));

    REQUIRE(agents.has_value());
    REQUIRE(agents->size() == 3);
    CHECK((*agents)[0].name == "user");
    CHECK((*agents)[0].source == "user");
    CHECK((*agents)[1].name == "extra");
    CHECK((*agents)[1].source == "extra");
    CHECK((*agents)[2].name == "project");
    CHECK((*agents)[2].source == "project");
}

TEST_CASE("agent definition registry supports lookup override and sorted listing")
{
    AgentDefinitionRegistry registry;
    registry.register_agent(
        AgentDefinition{
            .name = "reviewer",
            .description = "user reviewer",
            .source = "user",
        });
    registry.register_agent(
        AgentDefinition{
            .name = "tester",
            .description = "test runner",
            .source = "user",
        });
    registry.register_agent(
        AgentDefinition{
            .name = "reviewer",
            .description = "project reviewer",
            .source = "project",
        });

    REQUIRE(registry.get("reviewer") != nullptr);
    CHECK(registry.get("reviewer")->description == "project reviewer");
    CHECK(registry.get("reviewer")->source == "project");
    REQUIRE(registry.get("tester") != nullptr);
    CHECK(registry.get("missing") == nullptr);

    auto listed = registry.list();
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].name == "reviewer");
    CHECK(listed[1].name == "tester");
}

TEST_CASE("load_agent_definition_registry lets project override user definitions")
{
    TempDir temp{"codeharness-agent-registry-load-test"};
    const auto repo = temp.path / "repo";
    const auto user_agents = temp.path / "user-agents";
    const auto project_agents = repo / ".agents" / "agents";
    std::filesystem::create_directories(repo / ".git");

    write_text(user_agents / "reviewer.md", "---\ndescription: user reviewer\n---\nUser prompt\n");
    write_text(project_agents / "reviewer.md", "---\ndescription: project reviewer\n---\nProject prompt\n");

    AgentDefinitionLoadOptions options;
    options.load_default_user_agents = false;
    options.user_agent_dirs = {user_agents};
    options.project_agent_dirs = {".agents/agents"};

    auto registry = load_agent_definition_registry(repo, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("reviewer") != nullptr);
    CHECK(registry->get("reviewer")->description == "project reviewer");
    CHECK(registry->get("reviewer")->source == "project");
}

TEST_CASE("spawn config applies agent definition defaults and keeps explicit overrides")
{
    TempDir temp{"codeharness-spawn-config-apply-test"};

    AgentDefinition definition{
        .name = "reviewer",
        .description = "Review C++ changes",
        .system_prompt = "You are a careful reviewer.",
        .tools = {"read_file", "grep"},
        .disallowed_tools = {"bash"},
        .model = std::string{"claude-sonnet-4-6"},
        .effort = std::string{"high"},
        .permission_mode = std::string{"plan"},
        .max_turns = 5,
        .skills = {"review"},
        .mcp_servers = {"filesystem"},
        .source = "project",
        .path = temp.path / ".agents" / "agents" / "reviewer.md",
    };

    auto config = apply_agent_definition(
        TeammateSpawnConfig{
            .name = "",
            .team = "dev-team",
            .prompt = "Review this change",
            .cwd = temp.path,
            .model = std::string{"override-model"},
            .permissions = {"glob"},
            .skills = {"test"},
        },
        definition);

    CHECK(config.name == "reviewer");
    REQUIRE(config.model.has_value());
    CHECK(*config.model == "override-model");

    REQUIRE(config.system_prompt.has_value());
    CHECK(*config.system_prompt == "You are a careful reviewer.");

    CHECK(config.permissions == std::vector<std::string>{"glob"});
    CHECK(config.skills == std::vector<std::string>{"review", "test"});
    CHECK(config.disallowed_tools == std::vector<std::string>{"bash"});

    REQUIRE(config.effort.has_value());
    CHECK(*config.effort == "high");
    REQUIRE(config.permission_mode.has_value());
    CHECK(*config.permission_mode == "plan");
    REQUIRE(config.max_turns.has_value());
    CHECK(*config.max_turns == 5);
    CHECK(config.mcp_servers == std::vector<std::string>{"filesystem"});

    REQUIRE(config.agent_definition.has_value());
    CHECK(*config.agent_definition == "reviewer");
    REQUIRE(config.agent_definition_source.has_value());
    CHECK(*config.agent_definition_source == "project");
    REQUIRE(config.agent_definition_path.has_value());
    CHECK(*config.agent_definition_path == definition.path);
}

TEST_CASE("spawn config resolves agent definition from registry")
{
    TempDir temp{"codeharness-spawn-config-resolve-test"};

    AgentDefinitionRegistry registry;
    registry.register_agent(
        AgentDefinition{
            .name = "researcher",
            .description = "Research broadly",
            .system_prompt = "Search before answering.",
            .tools = {"read_file", "grep", "glob"},
            .source = "user",
        });

    auto resolved = resolve_spawn_config(
        TeammateSpawnConfig{
            .name = "worker",
            .team = "dev-team",
            .prompt = "Map the project",
            .cwd = temp.path,
        },
        registry,
        "researcher");

    REQUIRE(resolved.has_value());
    CHECK(resolved->name == "worker");
    CHECK(resolved->permissions == std::vector<std::string>{"read_file", "grep", "glob"});
    REQUIRE(resolved->system_prompt.has_value());
    CHECK(*resolved->system_prompt == "Search before answering.");
    REQUIRE(resolved->agent_definition.has_value());
    CHECK(*resolved->agent_definition == "researcher");

    auto missing = resolve_spawn_config(
        TeammateSpawnConfig{
            .name = "worker",
            .team = "dev-team",
            .prompt = "Map the project",
            .cwd = temp.path,
        },
        registry,
        "missing");

    CHECK(!missing.has_value());
    CHECK(missing.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("task notification renders XML envelope with escaped fields")
{
    auto record = codeharness::tasks::TaskRecord{
        .id = "a12345678",
        .type = codeharness::tasks::TaskType::LocalAgent,
        .status = codeharness::tasks::TaskStatus::Completed,
        .description = "Review <core> & tools",
        .created_at = "2026-06-04T00:00:00Z",
    };

    auto notification = make_task_notification(record, "Found <bug> & fixed");

    CHECK(notification.task_id == "a12345678");
    CHECK(notification.status == "completed");
    CHECK(notification.summary == "Review <core> & tools");
    CHECK(notification.result == "Found <bug> & fixed");

    const auto xml = task_notification_to_xml(notification);
    CHECK(xml.find("<task-notification>") != std::string::npos);
    CHECK(xml.find("<task-id>a12345678</task-id>") != std::string::npos);
    CHECK(xml.find("<status>completed</status>") != std::string::npos);
    CHECK(xml.find("<summary>Review &lt;core&gt; &amp; tools</summary>") != std::string::npos);
    CHECK(xml.find("<result>Found &lt;bug&gt; &amp; fixed</result>") != std::string::npos);
    CHECK(xml.find("</task-notification>") != std::string::npos);
}

TEST_CASE("task notification result message uses task_result mailbox type")
{
    auto record = codeharness::tasks::TaskRecord{
        .id = "a87654321",
        .type = codeharness::tasks::TaskType::LocalAgent,
        .status = codeharness::tasks::TaskStatus::Failed,
        .description = "",
        .created_at = "2026-06-04T00:00:00Z",
    };

    auto notification = make_task_notification(record, "retry with narrower scope", "QA worker failed");
    auto message = make_task_result_message("qa@dev-team", "leader@dev-team", notification);

    CHECK(message.type == codeharness::mailbox::MessageType::TaskResult);
    CHECK(message.sender_id == "qa@dev-team");
    CHECK(message.recipient_id == "leader@dev-team");
    CHECK(message.content.find("<task-id>a87654321</task-id>") != std::string::npos);
    CHECK(message.content.find("<status>failed</status>") != std::string::npos);
    CHECK(message.content.find("<summary>QA worker failed</summary>") != std::string::npos);
    CHECK(message.content.find("<result>retry with narrower scope</result>") != std::string::npos);
}

TEST_CASE("subprocess backend validates spawn config")
{
    TempDir temp{"codeharness-subprocess-validate-test"};
    codeharness::tasks::TaskManager task_manager{temp.path / "tasks"};
    codeharness::mailbox::TeamLifecycleManager team_manager{temp.path / "teams"};
    SubprocessBackend backend{task_manager, team_manager};

    auto empty_name = backend.spawn(
        TeammateSpawnConfig{
            .name = "",
            .team = "dev-team",
            .prompt = "Analyze code",
            .cwd = temp.path,
        });
    CHECK(!empty_name.has_value());
    CHECK(empty_name.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto empty_team = backend.spawn(
        TeammateSpawnConfig{
            .name = "worker",
            .team = "",
            .prompt = "Analyze code",
            .cwd = temp.path,
        });
    CHECK(!empty_team.has_value());
    CHECK(empty_team.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto empty_prompt = backend.spawn(
        TeammateSpawnConfig{
            .name = "worker",
            .team = "dev-team",
            .prompt = " ",
            .cwd = temp.path,
        });
    CHECK(!empty_prompt.has_value());
    CHECK(empty_prompt.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("subprocess backend requires existing team")
{
    TempDir temp{"codeharness-subprocess-missing-team-test"};
    codeharness::tasks::TaskManager task_manager{temp.path / "tasks"};
    codeharness::mailbox::TeamLifecycleManager team_manager{temp.path / "teams"};
    SubprocessBackend backend{task_manager, team_manager};

    auto result = backend.spawn(
        TeammateSpawnConfig{
            .name = "researcher",
            .team = "missing-team",
            .prompt = "Analyze code",
            .cwd = temp.path,
            .argv = codeharness_version_argv(),
        });

    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("subprocess backend spawns local agent task and adds team member")
{
    TempDir temp{"codeharness-subprocess-spawn-test"};
    codeharness::tasks::TaskManager task_manager{temp.path / "tasks"};
    codeharness::mailbox::TeamLifecycleManager team_manager{temp.path / "teams"};
    REQUIRE(team_manager.create_team("dev-team").has_value());

    SubprocessBackend backend{task_manager, team_manager};
    auto spawned = backend.spawn(
        TeammateSpawnConfig{
            .name = "researcher",
            .team = "dev-team",
            .prompt = "Analyze code",
            .cwd = temp.path,
            .argv = codeharness_version_argv(),
        });

    REQUIRE(spawned.has_value());
    CHECK(spawned->agent_id == "researcher@dev-team");
    CHECK(spawned->backend_type == "subprocess");
    CHECK(spawned->success == true);
    CHECK(!spawned->task_id.empty());

    auto completed = task_manager.wait_for_task(spawned->task_id);
    REQUIRE(completed.has_value());
    CHECK(completed->type == codeharness::tasks::TaskType::LocalAgent);
    CHECK(completed->metadata.at("agent_id") == "researcher@dev-team");
    CHECK(completed->metadata.at("team") == "dev-team");
    CHECK(completed->metadata.at("agent_name") == "researcher");
    CHECK(completed->metadata.at("backend_type") == "subprocess");

    auto team = team_manager.get_team("dev-team");
    REQUIRE(team.has_value());
    REQUIRE(team->has_value());
    REQUIRE(team->value().members.contains("researcher@dev-team"));
    const auto& member = team->value().members.at("researcher@dev-team");
    CHECK(member.name == "researcher");
    CHECK(member.backend_type == "subprocess");
    CHECK(!member.joined_at.empty());
}

TEST_CASE("subprocess backend records model system prompt skills and permissions metadata")
{
    TempDir temp{"codeharness-subprocess-metadata-test"};
    codeharness::tasks::TaskManager task_manager{temp.path / "tasks"};
    codeharness::mailbox::TeamLifecycleManager team_manager{temp.path / "teams"};
    REQUIRE(team_manager.create_team("qa-team").has_value());

    SubprocessBackend backend{task_manager, team_manager};
    auto spawned = backend.spawn(
        TeammateSpawnConfig{
            .name = "tester",
            .team = "qa-team",
            .prompt = "Run checks",
            .cwd = temp.path,
            .argv = codeharness_version_argv(),
            .model = std::string{"claude-sonnet-4-6"},
            .system_prompt = std::string{"You are a test runner."},
            .permissions = {"read_file", "grep"},
            .skills = {"test", "review"},
        });

    REQUIRE(spawned.has_value());
    auto completed = task_manager.wait_for_task(spawned->task_id);
    REQUIRE(completed.has_value());

    CHECK(completed->metadata.at("model") == "claude-sonnet-4-6");
    CHECK(completed->metadata.at("system_prompt") == "You are a test runner.");
    CHECK(completed->metadata.at("skills") == "test,review");
    CHECK(completed->metadata.at("permissions") == "read_file,grep");
}

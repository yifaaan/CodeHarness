#include "test_support.h"

#include "codeharness/coordinator/agent_definition.h"

using codeharness::coordinator::AgentDefinitionLoadOptions;
using codeharness::coordinator::discover_project_agent_dirs;
using codeharness::coordinator::load_agent_definition_file;
using codeharness::coordinator::load_agent_definitions;
using codeharness::coordinator::load_agent_definitions_from_dirs;
using codeharness::coordinator::parse_agent_definition_markdown;

namespace
{

auto write_text(const std::filesystem::path& path, std::string_view content) -> void
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.good());
    file << content;
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

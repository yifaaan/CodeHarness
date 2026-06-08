#include "test_support.h"

TEST_CASE("project context loader reads AGENTS and CLAUDE from parent to child")
{
    TempDir temp{"codeharness-project-context-order-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src" / "feature";
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(repo / ".git");

    {
        std::ofstream file{temp.path / "AGENTS.md", std::ios::binary};
        file << "outside";
    }

    {
        std::ofstream file{repo / "AGENTS.md", std::ios::binary};
        file << "repo agents";
    }

    {
        std::ofstream file{repo / "CLAUDE.md", std::ios::binary};
        file << "repo claude";
    }

    {
        std::ofstream file{child / "AGENTS.md", std::ios::binary};
        file << "child agents";
    }

    auto files = codeharness::load_project_context_files(child);

    REQUIRE(files.has_value());
    REQUIRE(files->size() == 3);
    CHECK(files->at(0).path == repo / "AGENTS.md");
    CHECK(files->at(0).content == "repo agents");
    CHECK(files->at(1).path == repo / "CLAUDE.md");
    CHECK(files->at(1).content == "repo claude");
    CHECK(files->at(2).path == child / "AGENTS.md");
    CHECK(files->at(2).content == "child agents");
}

TEST_CASE("project context loader applies a total character budget")
{
    TempDir temp{"codeharness-project-context-budget-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(repo / ".git");

    {
        std::ofstream file{repo / "AGENTS.md", std::ios::binary};
        file << "abcdef";
    }

    {
        std::ofstream file{child / "AGENTS.md", std::ios::binary};
        file << "ghijkl";
    }

    codeharness::ProjectContextOptions options;
    options.file_names = {"AGENTS.md"};
    options.max_total_chars = 8;

    auto files = codeharness::load_project_context_files(child, options);

    REQUIRE(files.has_value());
    REQUIRE(files->size() == 2);
    CHECK(files->at(0).path == repo / "AGENTS.md");
    CHECK(files->at(0).content == "abcdef");
    CHECK(files->at(1).path == child / "AGENTS.md");
    CHECK(files->at(1).content == "gh");
}

TEST_CASE("system prompt builder includes environment skills commands context and memory")
{
    TempDir temp{"codeharness-system-prompt-builder-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    std::filesystem::create_directories(child);
    init_git_repository_with_head(repo, "main");

    codeharness::SkillDefinition review_skill;
    review_skill.name = "review";
    review_skill.description = "Review code.";
    review_skill.source = "bundled";

    codeharness::SkillDefinition hidden_skill;
    hidden_skill.name = "hidden";
    hidden_skill.description = "Hidden skill.";
    hidden_skill.source = "user";
    hidden_skill.disable_model_invocation = true;

    const auto noop_handler = [](std::string_view) -> codeharness::Result<codeharness::CommandResult> {
        return codeharness::CommandResult{};
    };

    codeharness::PromptBuildRequest request;
    request.cwd = child;
    request.latest_user_prompt = "review this";
    request.available_skills = {review_skill, hidden_skill};
    request.available_commands = {
        codeharness::SlashCommand{.name = "skills", .description = "List loaded skills.", .handler = noop_handler},
        codeharness::SlashCommand{.name = "review", .description = "Invoke the review skill.", .handler = noop_handler},
    };
    request.project_context_files = {
        codeharness::ContextFile{.path = repo / "AGENTS.md", .content = "repo agents"},
        codeharness::ContextFile{.path = child / "AGENTS.md", .content = "child agents"},
    };
    request.relevant_memories = {codeharness::RelevantMemory{.title = "Build Notes", .content = "Use CMake."}};
    request.permission_mode = codeharness::PermissionMode::Default;

    auto prompt = codeharness::build_system_prompt(request);

    REQUIRE(prompt.has_value());
    CHECK(prompt->find("You are CodeHarness") != std::string::npos);
    CHECK(prompt->find("# Environment") != std::string::npos);
    CHECK(
        prompt->find("- Working directory: " + std::filesystem::weakly_canonical(child).string()) != std::string::npos);
    CHECK(prompt->find("- Date: ") != std::string::npos);
    CHECK(prompt->find("- Git repository: yes") != std::string::npos);
    CHECK(prompt->find("- Git branch: main") != std::string::npos);
    CHECK(prompt->find("- review [bundled]: Review code.") != std::string::npos);
    CHECK(prompt->find("Hidden skill.") == std::string::npos);
    CHECK(prompt->find("- /skills: List loaded skills.") != std::string::npos);
    CHECK(prompt->find("- /review: Invoke the review skill.") != std::string::npos);
    CHECK(prompt->find("repo agents") < prompt->find("child agents"));
    CHECK(prompt->find("# Relevant Memories") != std::string::npos);
    CHECK(prompt->find("Use CMake.") != std::string::npos);
}

#include "test_support.h"

TEST_CASE("skill markdown parser reads frontmatter metadata")
{
    auto skill = codeharness::parse_skill_markdown(
        "review",
        "---\n"
        "name: Code Review\n"
        "description: Review code carefully.\n"
        "aliases:\n"
        "  - review\n"
        "  - inspect\n"
        "user-invocable: no\n"
        "disable-model-invocation: yes\n"
        "model: careful-model\n"
        "argument-hint: FILE_OR_DIFF\n"
        "---\n"
        "# Review\n"
        "Instructions.\n",
        "user");

    CHECK(skill.name == "Code Review");
    CHECK(skill.description == "Review code carefully.");
    CHECK(skill.source == "user");
    CHECK(skill.aliases == std::vector<std::string>{"review", "inspect"});
    CHECK(skill.user_invocable == false);
    CHECK(skill.disable_model_invocation == true);
    CHECK(skill.model == "careful-model");
    CHECK(skill.argument_hint == "FILE_OR_DIFF");
}

TEST_CASE("skill markdown parser falls back to heading and first body line")
{
    auto skill = codeharness::parse_skill_markdown("debug", "# Debugging\n\nDiagnose failures methodically.\n");

    CHECK(skill.name == "Debugging");
    CHECK(skill.description == "Diagnose failures methodically.");
}

TEST_CASE("skill registry supports aliases and later overrides")
{
    codeharness::SkillRegistry registry;
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "bundled",
            .content = "bundled content",
            .source = "bundled",
            .aliases = {"inspect"},
        });
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "user",
            .content = "user content",
            .source = "user",
        });

    REQUIRE(registry.get("review") != nullptr);
    CHECK(registry.get("review")->content == "user content");
    REQUIRE(registry.get("inspect") != nullptr);
    CHECK(registry.get("inspect")->content == "bundled content");
}

TEST_CASE("project skill closest to cwd overrides parent skill")
{
    TempDir temp{"codeharness-project-skill-order-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src" / "feature";
    const auto repo_skill = repo / ".agents" / "skills" / "review";
    const auto child_skill = child / ".agents" / "skills" / "review";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(repo_skill);
    std::filesystem::create_directories(child_skill);

    {
        std::ofstream file{repo_skill / "SKILL.md", std::ios::binary};
        file << "# Review\n\nparent instructions\n";
    }

    {
        std::ofstream file{child_skill / "SKILL.md", std::ios::binary};
        file << "# Review\n\nchild instructions\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    auto registry = codeharness::load_skill_registry(child, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("review") != nullptr);
    CHECK(registry->get("review")->content.find("child instructions") != std::string::npos);
}

TEST_CASE("skill registry loads bundled skills by default")
{
    TempDir temp{"codeharness-bundled-skill-default-test"};

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;

    auto registry = codeharness::load_skill_registry(temp.path, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("commit") != nullptr);
    REQUIRE(registry->get("plan") != nullptr);
    REQUIRE(registry->get("review") != nullptr);
    REQUIRE(registry->get("debug") != nullptr);
    REQUIRE(registry->get("diagnose") != nullptr);
    REQUIRE(registry->get("simplify") != nullptr);
    REQUIRE(registry->get("skill-creator") != nullptr);
    REQUIRE(registry->get("test") != nullptr);
    CHECK(registry->get("review")->source == "bundled");
    CHECK(registry->get("skill-creator")->description.find("Create, improve, and verify") != std::string::npos);
}

TEST_CASE("user skill overrides bundled skill")
{
    TempDir temp{"codeharness-user-overrides-bundled-skill-test"};
    const auto skill_dir = temp.path / "skills" / "review";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: review\n"
             << "description: local review skill\n"
             << "---\n"
             << "# Review\n\n"
             << "user review instructions\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};

    auto registry = codeharness::load_skill_registry(temp.path, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("review") != nullptr);
    CHECK(registry->get("review")->source == "user");
    CHECK(registry->get("review")->content.find("user review instructions") != std::string::npos);
}

TEST_CASE("skill tool returns content and reports model invocation restrictions")
{
    codeharness::SkillRegistry registry;
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "review",
            .content = "review instructions",
            .source = "bundled",
        });
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "release",
            .description = "release",
            .content = "release instructions",
            .source = "user",
            .command_name = "release",
            .disable_model_invocation = true,
        });

    codeharness::SkillTool tool{registry};
    codeharness::ToolContext context;

    codeharness::ToolRequest review_request;
    review_request.id = "skill-review";
    review_request.name = "skill";
    set_request_input(review_request, R"({"name":"review"})");

    auto review = tool.execute(review_request, context);

    REQUIRE(review.has_value());
    CHECK(review->content == "review instructions");
    CHECK(review->is_error == false);

    codeharness::ToolRequest release_request;
    release_request.id = "skill-release";
    release_request.name = "skill";
    set_request_input(release_request, R"({"name":"release"})");

    auto release = tool.execute(release_request, context);

    REQUIRE(release.has_value());
    CHECK(release->content == "Skill release can only be invoked by the user as /release.");
    CHECK(release->is_error == true);
}

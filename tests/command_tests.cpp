#include "test_support.h"

#include <algorithm>

TEST_CASE("command registry resolves slash command names arguments and aliases")
{
    codeharness::CommandRegistry registry;
    registry.register_command(
        codeharness::SlashCommand{
            .name = "skills",
            .description = "list skills",
            .handler = [](std::string_view args) -> codeharness::Result<codeharness::CommandResult> {
                return codeharness::CommandResult{.message = std::string{args}};
            },
            .aliases = {"s"},
        });

    auto full = registry.lookup("/skills extra args");
    REQUIRE(full.command != nullptr);
    CHECK(full.command->name == "skills");
    CHECK(full.args == "extra args");

    auto alias = registry.lookup("/s");
    REQUIRE(alias.command != nullptr);
    CHECK(alias.command == full.command);

    auto missing = registry.lookup("/missing");
    CHECK(missing.command == nullptr);
}

TEST_CASE("plan mode commands are discoverable as message-only commands")
{
    codeharness::SkillRegistry skills;
    auto commands = codeharness::build_builtin_command_registry(skills);

    const auto plan = commands.lookup("/plan");
    REQUIRE(plan.command != nullptr);
    CHECK(plan.command->name == "plan");
    CHECK(plan.command->description.find("plan mode") != std::string::npos);
    CHECK(plan.command->invocation == codeharness::CommandInvocationKind::MessageOnly);

    const auto act = commands.lookup("/act");
    REQUIRE(act.command != nullptr);
    CHECK(act.command->name == "act");
    CHECK(act.command->description.find("default execution mode") != std::string::npos);
    CHECK(act.command->invocation == codeharness::CommandInvocationKind::MessageOnly);

    const auto full_auto = commands.lookup("/full_auto");
    REQUIRE(full_auto.command != nullptr);
    CHECK(full_auto.command->name == "fullauto");
    CHECK(full_auto.command->description.find("full-auto mode") != std::string::npos);
    CHECK(full_auto.command->invocation == codeharness::CommandInvocationKind::MessageOnly);

    const auto default_mode = commands.lookup("/default");
    REQUIRE(default_mode.command != nullptr);
    CHECK(default_mode.command->name == "default");
    CHECK(default_mode.command->description.find("default permission mode") != std::string::npos);
    CHECK(default_mode.command->invocation == codeharness::CommandInvocationKind::MessageOnly);

    const auto mode = commands.lookup("/mode");
    REQUIRE(mode.command != nullptr);
    CHECK(mode.command->name == "mode");
    CHECK(mode.command->description.find("permission mode") != std::string::npos);
    CHECK(mode.command->invocation == codeharness::CommandInvocationKind::MessageOnly);

    const auto listed = commands.list();
    CHECK(std::ranges::any_of(listed, [](const auto& command) { return command.name == "plan"; }));
    CHECK(std::ranges::any_of(listed, [](const auto& command) { return command.name == "act"; }));
    CHECK(std::ranges::any_of(listed, [](const auto& command) { return command.name == "fullauto"; }));
    CHECK(std::ranges::any_of(listed, [](const auto& command) { return command.name == "default"; }));
    CHECK(std::ranges::any_of(listed, [](const auto& command) { return command.name == "mode"; }));
}

TEST_CASE("skills command lists bundled user and project skills")
{
    TempDir temp{"codeharness-skills-command-list-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    const auto user_root = temp.path / "user-skills";
    const auto user_skill = user_root / "user-only";
    const auto project_skill = child / ".agents" / "skills" / "project-only";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(user_skill);
    std::filesystem::create_directories(project_skill);

    {
        std::ofstream file{user_skill / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: user-only\n"
             << "description: user skill description\n"
             << "---\n"
             << "# user-only\n";
    }

    {
        std::ofstream file{project_skill / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: project-only\n"
             << "description: project skill description\n"
             << "---\n"
             << "# project-only\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.user_skill_dirs = {user_root};
    auto skills = codeharness::load_skill_registry(child, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/skills ignored arguments");

    REQUIRE(result.has_value());
    REQUIRE(result->message.has_value());
    CHECK(result->message->find("- commit [bundled]:") != std::string::npos);
    CHECK(result->message->find("- plan [bundled]:") != std::string::npos);
    CHECK(result->message->find("- user-only [user]: user skill description") != std::string::npos);
    CHECK(result->message->find("- project-only [project]: project skill description") != std::string::npos);
}

TEST_CASE("plugin command lists loaded plugins")
{
    codeharness::SkillRegistry skills;
    std::vector<codeharness::LoadedPlugin> plugins;
    plugins.push_back(
        codeharness::LoadedPlugin{
            .manifest =
                codeharness::PluginManifest{
                    .name = "deploy-pack",
                    .version = "1.2.3",
                    .description = "Deployment helpers",
                },
            .enabled = true,
            .skills =
                {
                    codeharness::SkillDefinition{.name = "deploy"},
                },
            .commands =
                {
                    codeharness::PluginCommandDefinition{
                        .name = "release",
                        .command_name = "deploy-pack:release",
                    },
                },
            .mcp_servers =
                {
                    codeharness::McpStdioServerConfig{
                        .name = "demo",
                        .command = "python",
                    },
                },
        });

    auto commands = codeharness::build_builtin_command_registry(
        skills,
        codeharness::BuiltinCommandRegistryOptions{
            .plugins = std::span<const codeharness::LoadedPlugin>{plugins},
        });
    auto result = codeharness::execute_slash_command(commands, "/plugin list");

    REQUIRE(result.has_value());
    REQUIRE(result->message.has_value());
    CHECK(result->message->find("- deploy-pack [enabled] 1.2.3: Deployment helpers") != std::string::npos);
    CHECK(result->message->find("(skills: 1, commands: 1, mcp: 1)") != std::string::npos);
}

TEST_CASE("plugin markdown commands register as namespaced slash commands")
{
    codeharness::SkillRegistry skills;
    std::vector<codeharness::LoadedPlugin> plugins;
    plugins.push_back(
        codeharness::LoadedPlugin{
            .manifest = codeharness::PluginManifest{.name = "deploy-pack"},
            .enabled = true,
            .commands =
                {
                    codeharness::PluginCommandDefinition{
                        .name = "release",
                        .command_name = "deploy-pack:release",
                        .description = "Release from plugin.",
                        .content = "release $ARGUMENTS",
                        .source_plugin = "deploy-pack",
                        .model = "release-model",
                    },
                },
        });

    auto commands = codeharness::build_builtin_command_registry(
        skills,
        codeharness::BuiltinCommandRegistryOptions{
            .plugins = std::span<const codeharness::LoadedPlugin>{plugins},
        });
    auto result = codeharness::execute_slash_command(commands, "/deploy-pack:release staging");

    REQUIRE(result.has_value());
    CHECK(!result->message.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(*result->submit_prompt == "release staging");
    CHECK(result->submit_model == "release-model");
}

TEST_CASE("unknown slash command returns a clear error")
{
    codeharness::CommandRegistry registry;

    auto result = codeharness::execute_slash_command(registry, "/missing command");

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(result.error().message == "unknown command: /missing");
}

TEST_CASE("bundled user invocable skill registers as slash command")
{
    TempDir temp{"codeharness-bundled-skill-command-test"};

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/skill-creator create a deployment skill");

    REQUIRE(result.has_value());
    CHECK(!result->message.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("Base directory for this skill:") != std::string::npos);
    CHECK(result->submit_prompt->find("# skill-creator") != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: create a deployment skill") != std::string::npos);
}

TEST_CASE("skill slash command replaces argument placeholders and skill dir")
{
    TempDir temp{"codeharness-skill-command-placeholders-test"};
    const auto skill_dir = temp.path / "skills" / "deploy";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Deploy workflow.\n"
             << "---\n\n"
             << "# Deploy\n\n"
             << "plain: $ARGUMENTS\n"
             << "braced: ${ARGUMENTS}\n"
             << "dir: ${CLAUDE_SKILL_DIR}\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/deploy staging");

    REQUIRE(result.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("plain: staging") != std::string::npos);
    CHECK(result->submit_prompt->find("braced: staging") != std::string::npos);
    CHECK(result->submit_prompt->find("dir: " + skill_dir.string()) != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: staging") == std::string::npos);
}

TEST_CASE("project skill slash command appends arguments when no placeholder exists")
{
    TempDir temp{"codeharness-project-skill-command-args-test"};
    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    const auto skill_dir = child / ".agents" / "skills" / "shipit";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Ship workflow.\n"
             << "---\n\n"
             << "# Shipit\n\n"
             << "Ship this repo.\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    auto skills = codeharness::load_skill_registry(child, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/shipit now");

    REQUIRE(result.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("Base directory for this skill: " + skill_dir.string()) != std::string::npos);
    CHECK(result->submit_prompt->find("# Shipit") != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: now") != std::string::npos);
}

TEST_CASE("non user invocable skill is not registered as slash command")
{
    TempDir temp{"codeharness-hidden-skill-command-test"};
    const auto skill_dir = temp.path / "skills" / "hidden";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Hidden workflow.\n"
             << "user-invocable: false\n"
             << "---\n\n"
             << "# Hidden\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/hidden");

    REQUIRE(!result.has_value());
    CHECK(result.error().message == "unknown command: /hidden");
}

TEST_CASE("disable model invocation skill remains user slash invocable")
{
    TempDir temp{"codeharness-disable-model-skill-command-test"};
    const auto skill_dir = temp.path / "skills" / "deploy";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Deploy workflow.\n"
             << "disable-model-invocation: true\n"
             << "model: gpt-5.4\n"
             << "---\n\n"
             << "# Deploy\n\n"
             << "$ARGUMENTS\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/deploy staging");

    REQUIRE(result.has_value());
    CHECK(result->submit_model == "gpt-5.4");
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("staging") != std::string::npos);
}

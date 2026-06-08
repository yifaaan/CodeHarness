#include "test_support.h"

TEST_CASE("plugin loader parses manifests and loads enabled plugin skills")
{
    TempDir temp{"codeharness-plugin-skill-test"};
    const auto plugin_dir = temp.path / "plugins" / "deploy-pack";
    const auto skill_dir = plugin_dir / "skills" / "deploy";
    const auto command_dir = plugin_dir / "commands";
    std::filesystem::create_directories(skill_dir);
    std::filesystem::create_directories(command_dir);

    {
        std::ofstream manifest{plugin_dir / "plugin.json", std::ios::binary};
        manifest << "{\n"
                 << R"(  "name": "deploy-pack",)" << '\n'
                 << R"(  "version": "1.2.3",)" << '\n'
                 << R"(  "description": "Deployment helpers",)" << '\n'
                 << R"(  "skillsDir": "skills",)" << '\n'
                 << R"(  "commandsDir": "commands")" << '\n'
                 << "}\n";
    }

    {
        std::ofstream skill{skill_dir / "SKILL.md", std::ios::binary};
        skill << "---\n"
              << "name: deploy\n"
              << "description: Deploy from plugin.\n"
              << "---\n\n"
              << "# Deploy\n\n"
              << "plugin deploy instructions\n";
    }

    {
        std::ofstream command{command_dir / "release.md", std::ios::binary};
        command << "---\n"
                << "description: Release from plugin.\n"
                << "model: release-model\n"
                << "---\n\n"
                << "# Release\n\n"
                << "release $ARGUMENTS\n";
    }

    {
        std::ofstream mcp{plugin_dir / "mcp.json", std::ios::binary};
        mcp << "{\n"
            << R"(  "mcpServers": {)" << '\n'
            << R"(    "demo": {"type": "stdio", "command": "python", "args": ["demo.py"], "env": {"TOKEN": "abc"}},)"
            << '\n'
            << R"(    "docs": {)"
            << R"("type": "http", "url": "https://mcp.example.test", )"
            << R"("headers": {"Authorization": "Bearer token"})"
            << "}" << '\n'
            << "  }\n"
            << "}\n";
    }

    {
        std::ofstream hooks{plugin_dir / "hooks.json", std::ios::binary};
        hooks << "{\n"
              << R"(  "hooks": [)"
              << R"({"event":"pre_tool_use","type":"command","matcher":"write_file","config":{"command":"echo plugin-hook"}})"
              << "]\n"
              << "}\n";
    }

    codeharness::PluginLoadOptions options;
    options.load_default_user_plugins = false;
    options.user_plugin_roots = {temp.path / "plugins"};

    auto plugins = codeharness::load_plugins(temp.path, std::move(options));

    REQUIRE(plugins.has_value());
    REQUIRE(plugins->size() == 1);
    CHECK(plugins->front().manifest.name == "deploy-pack");
    CHECK(plugins->front().manifest.version == "1.2.3");
    CHECK(plugins->front().enabled);
    REQUIRE(plugins->front().skills.size() == 1);
    CHECK(plugins->front().skills.front().source == "plugin:deploy-pack");
    CHECK(plugins->front().skills.front().content.find("plugin deploy instructions") != std::string::npos);
    REQUIRE(plugins->front().commands.size() == 1);
    CHECK(plugins->front().commands.front().name == "release");
    CHECK(plugins->front().commands.front().command_name == "deploy-pack:release");
    CHECK(plugins->front().commands.front().description == "Release from plugin.");
    CHECK(plugins->front().commands.front().model == "release-model");
    REQUIRE(plugins->front().mcp_servers.size() == 2);
    REQUIRE(plugins->front().hooks.size() == 1);
    CHECK(plugins->front().hooks.front().event == codeharness::HookEvent::PreToolUse);
    CHECK(plugins->front().hooks.front().type == codeharness::HookType::Command);
    REQUIRE(plugins->front().hooks.front().matcher.has_value());
    CHECK(*plugins->front().hooks.front().matcher == "write_file");

    const codeharness::McpStdioServerConfig* stdio_server = nullptr;
    const codeharness::McpHttpServerConfig* http_server = nullptr;
    for (const auto& server : plugins->front().mcp_servers)
    {
        if (const auto* config = std::get_if<codeharness::McpStdioServerConfig>(&server);
            config != nullptr && config->name == "demo")
        {
            stdio_server = config;
        }
        if (const auto* config = std::get_if<codeharness::McpHttpServerConfig>(&server);
            config != nullptr && config->name == "docs")
        {
            http_server = config;
        }
    }

    REQUIRE(stdio_server != nullptr);
    CHECK(stdio_server->command == "python");
    CHECK(stdio_server->args == std::vector<std::string>{"demo.py"});
    CHECK(stdio_server->env.at("TOKEN") == "abc");

    REQUIRE(http_server != nullptr);
    CHECK(http_server->url == "https://mcp.example.test");
    CHECK(http_server->headers.at("Authorization") == "Bearer token");
}

TEST_CASE("disabled plugin does not contribute skills")
{
    TempDir temp{"codeharness-disabled-plugin-test"};
    const auto plugin_dir = temp.path / "plugins" / "disabled-pack";
    const auto skill_dir = plugin_dir / "skills" / "hidden";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream manifest{plugin_dir / "plugin.json", std::ios::binary};
        manifest << "{\n"
                 << R"(  "name": "disabled-pack",)" << '\n'
                 << R"(  "enabled_by_default": false)" << '\n'
                 << "}\n";
    }

    {
        std::ofstream skill{skill_dir / "SKILL.md", std::ios::binary};
        skill << "# Hidden\n\nshould not load\n";
    }

    codeharness::PluginLoadOptions options;
    options.load_default_user_plugins = false;
    options.user_plugin_roots = {temp.path / "plugins"};

    auto plugins = codeharness::load_plugins(temp.path, std::move(options));

    REQUIRE(plugins.has_value());
    REQUIRE(plugins->size() == 1);
    CHECK(!plugins->front().enabled);
    CHECK(plugins->front().skills.empty());
    CHECK(plugins->front().commands.empty());
    CHECK(plugins->front().mcp_servers.empty());
    CHECK(plugins->front().hooks.empty());
}

TEST_CASE("plugin loader rejects duplicate MCP server names across plugins")
{
    TempDir temp{"codeharness-duplicate-plugin-mcp-test"};
    const auto plugins_dir = temp.path / "plugins";

    const auto write_plugin = [](const std::filesystem::path& plugin_dir, std::string_view name) {
        std::filesystem::create_directories(plugin_dir);

        {
            std::ofstream manifest{plugin_dir / "plugin.json", std::ios::binary};
            manifest << "{\n"
                     << R"(  "name": ")" << name << "\"\n"
                     << "}\n";
        }

        {
            std::ofstream mcp{plugin_dir / "mcp.json", std::ios::binary};
            mcp << "{\n"
                << R"(  "mcpServers": {)" << '\n'
                << R"(    "demo": {"type": "stdio", "command": "python"})" << '\n'
                << "  }\n"
                << "}\n";
        }
    };

    write_plugin(plugins_dir / "alpha-pack", "alpha-pack");
    write_plugin(plugins_dir / "beta-pack", "beta-pack");

    codeharness::PluginLoadOptions options;
    options.load_default_user_plugins = false;
    options.user_plugin_roots = {plugins_dir};

    auto plugins = codeharness::load_plugins(temp.path, std::move(options));

    REQUIRE(!plugins.has_value());
    CHECK(plugins.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(plugins.error().message.find("duplicate plugin MCP server name: demo") != std::string::npos);
    CHECK(plugins.error().message.find("alpha-pack") != std::string::npos);
    CHECK(plugins.error().message.find("beta-pack") != std::string::npos);
}

TEST_CASE("plugin skills are merged into skill registry when plugin roots are enabled")
{
    TempDir temp{"codeharness-plugin-skill-registry-test"};
    const auto plugin_dir = temp.path / "plugins" / "review-pack";
    const auto skill_dir = plugin_dir / "skills" / "review";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream manifest{plugin_dir / "plugin.json", std::ios::binary};
        manifest << R"({"name":"review-pack","description":"Review plugin"})";
    }

    {
        std::ofstream skill{skill_dir / "SKILL.md", std::ios::binary};
        skill << "---\n"
              << "name: review\n"
              << "description: plugin review skill\n"
              << "---\n\n"
              << "# Review\n\n"
              << "plugin review instructions\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.plugin_options.load_default_user_plugins = false;
    options.plugin_options.user_plugin_roots = {temp.path / "plugins"};

    auto registry = codeharness::load_skill_registry(temp.path, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("review") != nullptr);
    CHECK(registry->get("review")->source == "plugin:review-pack");
    CHECK(registry->get("review")->content.find("plugin review instructions") != std::string::npos);
}

TEST_CASE("project plugins are ignored unless explicitly allowed")
{
    TempDir temp{"codeharness-project-plugin-default-off-test"};
    const auto plugin_dir = temp.path / ".codeharness" / "plugins" / "project-pack";
    const auto skill_dir = plugin_dir / "skills" / "project-only";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream manifest{plugin_dir / "plugin.json", std::ios::binary};
        manifest << R"({"name":"project-pack"})";
    }

    {
        std::ofstream skill{skill_dir / "SKILL.md", std::ios::binary};
        skill << "# Project Only\n\nproject plugin instructions\n";
    }

    codeharness::PluginLoadOptions disabled_options;
    disabled_options.load_default_user_plugins = false;
    auto disabled = codeharness::load_plugins(temp.path, disabled_options);

    REQUIRE(disabled.has_value());
    CHECK(disabled->empty());

    codeharness::PluginLoadOptions enabled_options;
    enabled_options.load_default_user_plugins = false;
    enabled_options.allow_project_plugins = true;
    auto enabled = codeharness::load_plugins(temp.path, enabled_options);

    REQUIRE(enabled.has_value());
    REQUIRE(enabled->size() == 1);
    CHECK(enabled->front().manifest.name == "project-pack");
}

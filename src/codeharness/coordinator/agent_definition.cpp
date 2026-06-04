#include "codeharness/coordinator/agent_definition.h"

#include "codeharness/core/paths.h"
#include "codeharness/core/strings.h"
#include "codeharness/skills/skill_yaml.h"
#include "codeharness/tools/text_file.h"

#include <glob/glob.h>
#include <nonstd/expected.hpp>
#include <simdutf.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <string_view>
#include <system_error>
#include <utility>

namespace codeharness::coordinator
{

namespace
{

using skills::yaml_get_string;
using skills::yaml_get_string_list;

struct ParsedAgentMarkdown
{
    YAML::Node frontmatter;
    std::string_view body;
};

// 解析 Markdown frontmatter。
auto parse_frontmatter(std::string_view content) -> ParsedAgentMarkdown
{
    auto [first_line, offset] = next_line(content, 0);
    if (trim(first_line) != "---")
    {
        return ParsedAgentMarkdown{.body = content};
    }

    const auto frontmatter_start = offset;
    while (offset < content.size())
    {
        const auto line_start = offset;
        auto [line, next_offset] = next_line(content, offset);
        offset = next_offset;

        if (trim(line) != "---")
        {
            continue;
        }

        const auto raw_yaml = content.substr(frontmatter_start, line_start - frontmatter_start);
        YAML::Node frontmatter;
        try
        {
            frontmatter = YAML::Load(std::string{raw_yaml});
        }
        catch (const YAML::Exception& e)
        {
            spdlog::warn("failed to parse agent definition frontmatter yaml: {}", e.what());
        }

        return ParsedAgentMarkdown{
            .frontmatter = std::move(frontmatter),
            .body = content.substr(offset),
        };
    }

    return ParsedAgentMarkdown{.body = content};
}

// 在 UTF-8 字符边界安全截断，避免 description 预览切出半个 code point。
auto utf8_safe_truncate(std::string_view s, std::size_t max_len) -> std::string_view
{
    if (s.size() <= max_len)
    {
        return s;
    }

    const auto prefix = simdutf::validate_utf8_with_errors(s.data(), max_len);
    if (prefix.error == simdutf::error_code::SUCCESS)
    {
        return s.substr(0, max_len);
    }

    return s.substr(0, prefix.count);
}

auto first_body_line(std::string_view body) -> std::optional<std::string>
{
    std::size_t offset = 0;
    while (offset < body.size())
    {
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;

        const auto stripped = trim(line);
        if (!stripped.empty() && !stripped.starts_with('#'))
        {
            return std::string{utf8_safe_truncate(stripped, 200)};
        }
    }

    return std::nullopt;
}

auto heading_from_body(std::string_view body) -> std::optional<std::string>
{
    std::size_t offset = 0;
    while (offset < body.size())
    {
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;

        const auto stripped = trim(line);
        if (!stripped.starts_with("# "))
        {
            continue;
        }

        auto heading = std::string{trim(stripped.substr(2))};
        if (!heading.empty())
        {
            return heading;
        }
    }

    return std::nullopt;
}

auto yaml_get_positive_int(const YAML::Node& node, std::string_view key) -> std::optional<int>
{
    const auto value = node[std::string{key}];
    if (!value || !value.IsScalar())
    {
        return std::nullopt;
    }

    try
    {
        const auto parsed = value.as<int>();
        if (parsed > 0)
        {
            return parsed;
        }
    }
    catch (const YAML::Exception&)
    {
    }

    return std::nullopt;
}

// 支持 snake_case 与 camelCase 两种字段名，降低和上游/插件定义互操作的摩擦。
auto yaml_get_string_alias(const YAML::Node& node, std::string_view snake_key, std::string_view camel_key)
    -> std::optional<std::string>
{
    auto value = yaml_get_string(node, snake_key);
    if (value)
    {
        return value;
    }

    return yaml_get_string(node, camel_key);
}

auto yaml_get_string_list_alias(const YAML::Node& node, std::string_view snake_key, std::string_view camel_key)
    -> std::vector<std::string>
{
    auto values = yaml_get_string_list(node, snake_key);
    if (!values.empty())
    {
        return values;
    }

    return yaml_get_string_list(node, camel_key);
}

auto yaml_get_positive_int_alias(const YAML::Node& node, std::string_view snake_key, std::string_view camel_key)
    -> std::optional<int>
{
    auto value = yaml_get_positive_int(node, snake_key);
    if (value)
    {
        return value;
    }

    return yaml_get_positive_int(node, camel_key);
}

auto find_git_root(const std::filesystem::path& start) -> std::optional<std::filesystem::path>
{
    auto current = start;
    while (true)
    {
        std::error_code error;
        if (std::filesystem::exists(current / ".git", error))
        {
            return current;
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            return std::nullopt;
        }

        current = parent;
    }
}

} // namespace

auto default_user_agent_dirs() -> std::vector<std::filesystem::path>
{
    const auto home = home_directory();
    if (!home)
    {
        return {};
    }

    return {
        *home / ".codeharness" / "agents",
        *home / ".openharness" / "agents",
        *home / ".claude" / "agents",
        *home / ".agents" / "agents",
    };
}

auto AgentDefinitionRegistry::register_agent(AgentDefinition definition) -> void
{
    if (definition.name.empty())
    {
        return;
    }

    auto stored = std::make_shared<AgentDefinition>(std::move(definition));
    by_name_[stored->name] = std::move(stored);
}

auto AgentDefinitionRegistry::get(std::string_view name) const -> const AgentDefinition*
{
    const auto it = by_name_.find(std::string{name});
    if (it == by_name_.end())
    {
        return nullptr;
    }

    return it->second.get();
}

auto AgentDefinitionRegistry::list() const -> std::vector<AgentDefinition>
{
    std::vector<AgentDefinition> result;
    result.reserve(by_name_.size());

    for (const auto& [_, definition] : by_name_)
    {
        result.push_back(*definition);
    }

    std::ranges::sort(result, {}, &AgentDefinition::name);
    return result;
}

auto parse_agent_definition_markdown(std::string_view default_name, std::string content, std::string source)
    -> AgentDefinition
{
    const auto parsed = parse_frontmatter(content);

    auto name = yaml_get_string(parsed.frontmatter, "name").value_or(std::string{});
    if (name.empty())
    {
        name = heading_from_body(parsed.body).value_or(std::string{default_name});
    }

    auto description = yaml_get_string(parsed.frontmatter, "description").value_or(std::string{});
    if (description.empty())
    {
        description = first_body_line(parsed.body).value_or("Agent: " + name);
    }

    return AgentDefinition{
        .name = std::move(name),
        .description = std::move(description),
        .system_prompt = std::string{trim(parsed.body)},
        .tools = yaml_get_string_list(parsed.frontmatter, "tools"),
        .disallowed_tools = yaml_get_string_list_alias(parsed.frontmatter, "disallowed_tools", "disallowedTools"),
        .model = yaml_get_string(parsed.frontmatter, "model"),
        .effort = yaml_get_string(parsed.frontmatter, "effort"),
        .permission_mode = yaml_get_string_alias(parsed.frontmatter, "permission_mode", "permissionMode"),
        .max_turns = yaml_get_positive_int_alias(parsed.frontmatter, "max_turns", "maxTurns"),
        .skills = yaml_get_string_list(parsed.frontmatter, "skills"),
        .mcp_servers = yaml_get_string_list_alias(parsed.frontmatter, "mcp_servers", "mcpServers"),
        .source = std::move(source),
    };
}

auto load_agent_definition_file(const std::filesystem::path& path, std::string source) -> Result<AgentDefinition>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    auto agent = parse_agent_definition_markdown(path.stem().string(), std::move(*content), std::move(source));
    agent.path = path;
    agent.base_dir = path.parent_path();
    return agent;
}

auto load_agent_definitions_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> Result<std::vector<AgentDefinition>>
{
    std::vector<AgentDefinition> agents;
    std::set<std::filesystem::path> seen;

    for (const auto& root : directories)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            continue;
        }

        std::vector<std::filesystem::path> matches;
        try
        {
            for (const auto& candidate : glob::glob(root.string() + "/*.md"))
            {
                auto canonical = std::filesystem::weakly_canonical(candidate, error);
                if (error)
                {
                    return fail<std::vector<AgentDefinition>>(ErrorKind::Io,
                        "failed to resolve agent definition path: " + error.message());
                }

                if (seen.insert(canonical).second)
                {
                    matches.push_back(std::move(canonical));
                }
            }
        }
        catch (const std::exception& e)
        {
            return fail<std::vector<AgentDefinition>>(ErrorKind::Io,
                "failed to scan agent definitions: " + std::string{e.what()});
        }

        std::ranges::sort(matches);
        agents.reserve(agents.size() + matches.size());
        const std::string source_str{source};
        for (const auto& path : matches)
        {
            auto agent = load_agent_definition_file(path, source_str);
            if (!agent)
            {
                return nonstd::make_unexpected(agent.error());
            }
            agents.push_back(std::move(*agent));
        }
    }

    return agents;
}

auto discover_project_agent_dirs(const std::filesystem::path& cwd,
                                 std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>
{
    std::error_code error;
    auto current = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::vector<std::filesystem::path>>(ErrorKind::Io,
            "failed to resolve cwd for agent discovery: " + error.message());
    }

    const auto git_root = find_git_root(current);
    std::vector<std::filesystem::path> discovered;

    while (true)
    {
        for (const auto& relative : relative_dirs)
        {
            if (!is_safe_relative_path(relative))
            {
                return fail<std::vector<std::filesystem::path>>(ErrorKind::InvalidArgument,
                    "project agent dir must be a safe relative path: " + relative.string());
            }

            const auto candidate = current / relative;
            if (std::filesystem::is_directory(candidate, error))
            {
                discovered.push_back(candidate);
            }
        }

        if (git_root && current == *git_root)
        {
            break;
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            break;
        }

        current = parent;
    }

    // 扫描时是 cwd → parent；反转成 parent → cwd，让更近目录在后面覆盖更远目录。
    std::ranges::reverse(discovered);
    return discovered;
}

auto load_agent_definitions(const std::filesystem::path& cwd, AgentDefinitionLoadOptions options)
    -> Result<std::vector<AgentDefinition>>
{
    std::vector<AgentDefinition> agents;

    auto append_loaded = [&](std::span<const std::filesystem::path> dirs, std::string_view source) -> Result<void> {
        auto loaded = load_agent_definitions_from_dirs(dirs, source);
        if (!loaded)
        {
            return nonstd::make_unexpected(loaded.error());
        }

        agents.reserve(agents.size() + loaded->size());
        std::ranges::move(*loaded, std::back_inserter(agents));
        return {};
    };

    if (options.load_default_user_agents)
    {
        auto defaults = default_user_agent_dirs();
        if (auto loaded = append_loaded(defaults, "user"); !loaded)
        {
            return nonstd::make_unexpected(loaded.error());
        }
    }

    if (auto loaded = append_loaded(options.user_agent_dirs, "user"); !loaded)
    {
        return nonstd::make_unexpected(loaded.error());
    }

    if (auto loaded = append_loaded(options.extra_agent_dirs, "extra"); !loaded)
    {
        return nonstd::make_unexpected(loaded.error());
    }

    if (options.allow_project_agents)
    {
        auto project_dirs = discover_project_agent_dirs(cwd, options.project_agent_dirs);
        if (!project_dirs)
        {
            return nonstd::make_unexpected(project_dirs.error());
        }

        if (auto loaded = append_loaded(*project_dirs, "project"); !loaded)
        {
            return nonstd::make_unexpected(loaded.error());
        }
    }

    return agents;
}

auto load_agent_definition_registry(const std::filesystem::path& cwd, AgentDefinitionLoadOptions options)
    -> Result<AgentDefinitionRegistry>
{
    auto definitions = load_agent_definitions(cwd, std::move(options));
    if (!definitions)
    {
        return nonstd::make_unexpected(definitions.error());
    }

    AgentDefinitionRegistry registry;
    for (auto& definition : *definitions)
    {
        registry.register_agent(std::move(definition));
    }

    return registry;
}

} // namespace codeharness::coordinator

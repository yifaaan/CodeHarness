#include "codeharness/coordinator/spawn_config.h"

#include "codeharness/core/strings.h"

#include <algorithm>
#include <utility>

namespace codeharness::coordinator
{
namespace
{

auto append_unique(std::vector<std::string>& target, const std::vector<std::string>& values) -> void
{
    for (const auto& value : values)
    {
        if (std::ranges::find(target, value) == target.end())
        {
            target.push_back(value);
        }
    }
}

} // namespace

auto apply_agent_definition(TeammateSpawnConfig config, const AgentDefinition& definition)
    -> TeammateSpawnConfig
{
    if (trim(config.name).empty())
    {
        config.name = definition.name;
    }

    if (!config.model && definition.model)
    {
        config.model = definition.model;
    }

    if (!config.system_prompt && !trim(definition.system_prompt).empty())
    {
        config.system_prompt = definition.system_prompt;
    }

    if (config.permissions.empty())
    {
        config.permissions = definition.tools;
    }

    auto requested_skills = std::move(config.skills);
    config.skills.clear();
    append_unique(config.skills, definition.skills);
    append_unique(config.skills, requested_skills);

    if (config.disallowed_tools.empty())
    {
        config.disallowed_tools = definition.disallowed_tools;
    }

    if (!config.effort && definition.effort)
    {
        config.effort = definition.effort;
    }

    if (!config.permission_mode && definition.permission_mode)
    {
        config.permission_mode = definition.permission_mode;
    }

    if (!config.max_turns && definition.max_turns)
    {
        config.max_turns = definition.max_turns;
    }

    if (config.mcp_servers.empty())
    {
        config.mcp_servers = definition.mcp_servers;
    }

    config.agent_definition = definition.name;
    config.agent_definition_source = definition.source;
    if (!definition.path.empty())
    {
        config.agent_definition_path = definition.path;
    }

    return config;
}

auto resolve_spawn_config(TeammateSpawnConfig config,
                          const AgentDefinitionRegistry& registry,
                          std::string_view agent_type)
    -> Result<TeammateSpawnConfig>
{
    const auto type = trim(agent_type);
    if (type.empty())
    {
        return config;
    }

    const auto* definition = registry.get(type);
    if (definition == nullptr)
    {
        return fail<TeammateSpawnConfig>(
            ErrorKind::InvalidArgument,
            "agent definition not found: " + std::string{type});
    }

    return apply_agent_definition(std::move(config), *definition);
}

} // namespace codeharness::coordinator

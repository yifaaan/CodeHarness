//==============================================================================
// spawn_config.cpp — Agent spawn 配置解析
//
// 核心函数 resolve_spawn_config 的完整算法：
//
//   输入：TeammateSpawnConfig + AgentDefinitionRegistry + agent_type
//
//   1. agent_type 为空 → 不需要解析定义，直接返回 config（原样）
//   2. agent_type 非空 → 从 registry 查找对应的 AgentDefinition
//   3. apply_agent_definition(config, definition)：
//      逐一检查 config 中的字段，如果为空则使用 definition 中的值填充。
//
//   覆盖规则（definition 字段作为"默认值"，config 字段优先）：
//     - name: 如果 config.name 为空，使用 definition.name
//     - model: 如果 config.model 为空且 definition.model 存在，使用
//     - system_prompt: 如果 config.system_prompt 为空，使用 definition 的
//     - tools/permissions: definition 和 config 的合并（append_unique）
//     - skills: definition 的 skills 在前，config 的 skills 在后（append_unique）
//     - agent_definition: 自动设置为 definition.name，用于溯源
//
//   为什么需要 apply_agent_definition？
//     如果用显式参数（prompt/model/tools）创建子 agent，不需要 definition。
//     如果用类型名（subagent_type = "write-tui"）创建，则需要查找定义文件
//     并应用其默认值。这样用户可以在 .agents/**/*.md 中定义 agent 模板，
//     然后在 prompt 中只需引用类型名即可。
//==============================================================================

#include "codeharness/coordinator/spawn_config.h"

#include "codeharness/core/strings.h"

#include <algorithm>
#include <string>
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

auto normalized_agent_name(std::string_view name) -> std::string
{
    const auto trimmed = trim(name);
    if (trimmed.empty())
    {
        return std::string{default_agent_name};
    }

    return std::string{trimmed};
}

auto normalized_team_name(std::string_view team) -> std::string
{
    const auto trimmed = trim(team);
    if (trimmed.empty())
    {
        return std::string{default_team_name};
    }

    return std::string{trimmed};
}

auto make_agent_id(std::string_view name, std::string_view team) -> std::string
{
    return normalized_agent_name(name) + "@" + normalized_team_name(team);
}

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

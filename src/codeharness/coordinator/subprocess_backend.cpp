//==============================================================================
// subprocess_backend.cpp — 子进程后端
//
// 架构角色：process spawn 层
// 职责：将 TeammateSpawnConfig 转化为实际的子进程创建操作。这是
//       coordinator 中"干活"的部分——前面都是配置解析和校验，到这里
//       才真正创建 task 和注册 team member。
//
// 设计原理：
//   SubprocessBackend 本身不启动子进程——它调用 TaskManager::create_agent_task
//   让 TaskManager 去创建和启动。Backend 只负责：
//   1. 验证 config（名称、team、prompt 不能为空）
//   2. 规范名称（normalized_agent_name/team_name）
//   3. 将 config 中的所有可选项序列化为 metadata map（用于 JSON 持久化）
//   4. 调用 TaskManager 创建 agent 任务
//   5. 将 agent 注册为 team 成员
//
//   为什么叫"Backend"？因为未来可能支持其他 spawn 方式（如 Docker
//   容器、SSH 远程等），通过不同的 Backend 实现。但目前只有 subprocess。
//==============================================================================

#include "codeharness/coordinator/subprocess_backend.h"

#include "codeharness/coordinator/spawn_config.h"

#include "codeharness/core/strings.h"

#include <fmt/ranges.h>
#include <nonstd/expected.hpp>

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace codeharness::coordinator
{

namespace
{

auto join_csv(std::span<const std::string> values) -> std::string
{
    return fmt::format("{}", fmt::join(values, ","));
}

auto validate_spawn_config(const TeammateSpawnConfig& config) -> Result<void>
{
    if (trim(config.name).empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "subprocess spawn requires non-empty agent name");
    }
    if (trim(config.team).empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "subprocess spawn requires non-empty team name");
    }
    if (trim(config.prompt).empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "subprocess spawn requires non-empty prompt");
    }

    return {};
}

} // namespace

SubprocessBackend::SubprocessBackend(tasks::TaskManager& task_manager, mailbox::TeamLifecycleManager& team_manager) : task_manager_{task_manager}, team_manager_{team_manager}
{
}

auto SubprocessBackend::spawn(const TeammateSpawnConfig& config) -> Result<SpawnResult>
{
    if (auto valid = validate_spawn_config(config); !valid)
    {
        return nonstd::make_unexpected(valid.error());
    }

    const auto name = normalized_agent_name(config.name);
    const auto team_name = normalized_team_name(config.team);
    const auto prompt = std::string{trim(config.prompt)};
    const auto agent_id = make_agent_id(name, team_name);

    auto team = team_manager_.get_team(team_name);
    if (!team)
    {
        return nonstd::make_unexpected(team.error());
    }
    if (!team->has_value())
    {
        return fail<SpawnResult>(ErrorKind::InvalidArgument, "subprocess spawn team does not exist: " + team_name);
    }

    auto metadata = std::map<std::string, std::string>{
        {"agent_id", agent_id},
        {"agent_name", name},
        {"team", team_name},
        {"backend_type", "subprocess"},
    };

    if (config.model)
    {
        metadata["model"] = *config.model;
    }
    if (config.system_prompt)
    {
        metadata["system_prompt"] = *config.system_prompt;
    }
    if (!config.skills.empty())
    {
        metadata["skills"] = join_csv(config.skills);
    }
    if (!config.permissions.empty())
    {
        metadata["permissions"] = join_csv(config.permissions);
    }
    if (!config.disallowed_tools.empty())
    {
        metadata["disallowed_tools"] = join_csv(config.disallowed_tools);
    }
    if (config.effort)
    {
        metadata["effort"] = *config.effort;
    }
    if (config.permission_mode)
    {
        metadata["permission_mode"] = *config.permission_mode;
    }
    if (config.max_turns)
    {
        metadata["max_turns"] = std::to_string(*config.max_turns);
    }
    if (!config.mcp_servers.empty())
    {
        metadata["mcp_servers"] = join_csv(config.mcp_servers);
    }
    if (config.agent_definition)
    {
        metadata["agent_definition"] = *config.agent_definition;
    }
    if (config.agent_definition_source)
    {
        metadata["agent_definition_source"] = *config.agent_definition_source;
    }
    if (config.agent_definition_path)
    {
        metadata["agent_definition_path"] = config.agent_definition_path->string();
    }

    auto task = task_manager_.create_agent_task(
        tasks::AgentTaskSpec{
            .description = trim(config.description).empty() ? "Agent " + agent_id : std::string{trim(config.description)},
            .cwd = config.cwd,
            .prompt = prompt,
            .command = config.command,
            .argv = config.argv,
            .env = config.env,
            .model = config.model,
            .metadata = std::move(metadata),
        });
    if (!task)
    {
        return nonstd::make_unexpected(task.error());
    }

    auto updated_team = team_manager_.add_member(
        team_name,
        mailbox::TeamMember{
            .agent_id = agent_id,
            .name = name,
            .backend_type = "subprocess",
        });
    if (!updated_team)
    {
        return nonstd::make_unexpected(updated_team.error());
    }

    return SpawnResult{
        .task_id = task->id,
        .agent_id = agent_id,
        .backend_type = "subprocess",
        .success = true,
    };
}

} // namespace codeharness::coordinator

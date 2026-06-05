#pragma once

#include "codeharness/core/result.h"
#include "codeharness/mailbox/team_lifecycle.h"
#include "codeharness/tasks/task_manager.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::coordinator
{

//==============================================================================
// subprocess_backend.h — Agent spawn 配置和后端
//
// TeammateSpawnConfig：agent spawn 的完整配置参数
//   这是 config 解析链中的"最终格式"：
//     AgentSpawnRequest（来自 prompt/tool 调用的 JSON）
//       → resolve_spawn_config（应用 AgentDefinition）
//         → TeammateSpawnConfig（可以送到 SubprocessBackend::spawn）
//
//   字段说明：
//     name/team — agent 标识，构成 agent_id = "name@team"
//     prompt    — 子 agent 的任务指令（自然语言）
//     command/argv/env — 子进程执行参数（可覆盖默认 "codeharness -p ..."）
//     model     — 指定子 agent 使用的 LLM 模型
//     system_prompt / permissions / skills / disallowed_tools / effort /
//       permission_mode / max_turns / mcp_servers
//       — 这些传到子 agent 的 metadata 中，子 agent 启动时读取
//     agent_definition* — 溯源字段，记录此配置来自哪个定义文件
//
// SpawnResult：spawn 操作的返回结果
//   backend_type = "subprocess"（目前唯一实现，扩展只改这里）
//
// SubprocessBackend：
//   构造函数接收 task_manager 和 team_manager 的引用（不拥有）
//   spawn 操作将 agent 任务注册到 task_manager，将 agent 成员添加到 team
//==============================================================================
struct TeammateSpawnConfig
{
    std::string name;
    std::string team;
    std::string description;
    std::string prompt;
    std::filesystem::path cwd;

    std::optional<std::string> command;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
    std::optional<std::string> model;
    std::optional<std::string> system_prompt;
    std::vector<std::string> permissions;
    std::vector<std::string> skills;

    std::vector<std::string> disallowed_tools;
    std::optional<std::string> effort;
    std::optional<std::string> permission_mode;
    std::optional<int> max_turns;
    std::vector<std::string> mcp_servers;

    std::optional<std::string> agent_definition;
    std::optional<std::string> agent_definition_source;
    std::optional<std::filesystem::path> agent_definition_path;
};

struct SpawnResult
{
    std::string task_id;
    std::string agent_id;
    std::string backend_type = "subprocess";
    bool success = true;
    std::optional<std::string> error;
};

class SubprocessBackend
{
public:
    SubprocessBackend(tasks::TaskManager& task_manager, mailbox::TeamLifecycleManager& team_manager);

    auto spawn(const TeammateSpawnConfig& config) -> Result<SpawnResult>;

private:
    tasks::TaskManager& task_manager_;
    mailbox::TeamLifecycleManager& team_manager_;
};

} // namespace codeharness::coordinator

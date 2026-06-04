#pragma once

#include "codeharness/core/result.h"
#include "codeharness/mailbox/team_lifecycle.h"
#include "codeharness/tasks/task_manager.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// SubprocessBackend —— coordinator 创建本地 worker agent 的最小后端
//
// 当前 CodeHarness 已经有几块基础设施：
//   - TaskManager：能启动一次性 local_agent 子进程。
//   - TeamLifecycleManager：能在 team.json 中记录团队成员。
//   - Mailbox/send_message：能给已有 task/agent 投递消息。
//
// SubprocessBackend 的职责是把“创建一个 worker”这件事串起来：
//   1. 校验 spawn 配置；
//   2. 确认 team 已经存在；
//   3. 调 TaskManager::create_agent_task 启动 worker；
//   4. 调 TeamLifecycleManager::add_member 记录 team membership；
//   5. 返回 task_id + agent_id。
//
// 它不拥有 TaskManager/TeamLifecycleManager，只保存引用。这样 runtime 里只有一份
// task/team 状态，不会因为 backend 内部复制 manager 而产生状态分裂。

namespace codeharness::coordinator
{

struct TeammateSpawnConfig
{
    std::string name;              // worker 名，例如 "researcher"
    std::string team;              // team 名，例如 "dev-team"
    std::string prompt;            // 初始任务 prompt
    std::filesystem::path cwd;     // worker 工作目录

    std::optional<std::string> command; // 可选启动命令覆盖；缺失时由 TaskManager 使用默认 codeharness argv
    std::vector<std::string> argv;       // 可选 argv 覆盖；主要用于测试或显式指定 worker 可执行文件
    std::optional<std::string> model;
    std::optional<std::string> system_prompt;
    std::vector<std::string> permissions; // 第一版只记录到 metadata，不做权限同步
    std::vector<std::string> skills;      // 第一版只记录到 metadata，不主动加载 skill
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

//==============================================================================
// runtime.h — Coordinator Runtime（协调器运行时）
//
// 架构角色：coordination 层核心
// 职责：整合任务管理、团队生命周期、mailbox、agent 定义、子进程后端
//       五大子系统，提供 agent spawn 和 mailbox 操作的高层接口。
//
// 设计原理：
//   CoordinatorRuntime 是"agent farm"的总控。它的角色类似操作系统的
//   init 进程——管理子 agent 的创建、通信、结果收集。
//
//   核心能力：
//   1. spawn_agent：根据 AgentSpawnRequest 创建子 agent。
//      流程：验证 mode → 解析 spawn config → 确保 team 存在 →
//      通过 SubprocessBackend 创建进程 → 返回 AgentSpawnResponse
//   2. publish_task_result：子 agent 完成或失败后，将结果封装为
//      mailbox 消息，发送给指定的 recipient（通常是 coordinator 自己）
//   3. drain_coordinator_mailbox：消费 coordinator inbox 中的消息，
//      分类返回（user_messages / task_results / shutdown 等）
//
//   子组件的所有权：
//     CoordinatorRuntime 直接持有各子组件（TaskManager, TeamLifecycleManager,
//     Mailbox, SubprocessBackend）。这些组件的生命周期与 Runtime 实例一致。
//     SubprocessBackend 持有 TaskManager 和 TeamManager 的引用（而非所有权），
//     因为 Backend 只是它们的用户，不是所有者。
//
//   create_default_runtime：静态工厂函数，负责用默认路径初始化所有子系统：
//     task_root  → ~/.codeharness/data/tasks/
//     team_root  → ~/.codeharness/teams/
//     mailbox_root → ~/.codeharness/mailbox/
//     agent definitions → 从用户目录和项目目录加载
//==============================================================================

#pragma once

#include "codeharness/coordinator/agent_definition.h"
#include "codeharness/coordinator/subprocess_backend.h"
#include "codeharness/core/result.h"
#include "codeharness/mailbox/mailbox.h"
#include "codeharness/mailbox/message_consumer.h"
#include "codeharness/mailbox/team_lifecycle.h"
#include "codeharness/tasks/task_manager.h"
#include "codeharness/tasks/task_tools.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace codeharness::coordinator
{

class CoordinatorRuntime
{
public:
    CoordinatorRuntime(std::filesystem::path task_root,
                       std::filesystem::path team_root,
                       std::filesystem::path mailbox_root,
                       AgentDefinitionRegistry agent_definitions = {});

    CoordinatorRuntime(const CoordinatorRuntime&) = delete;
    auto operator=(const CoordinatorRuntime&) -> CoordinatorRuntime& = delete;
    CoordinatorRuntime(CoordinatorRuntime&&) = delete;
    auto operator=(CoordinatorRuntime&&) -> CoordinatorRuntime& = delete;

    [[nodiscard]] auto task_manager() noexcept -> tasks::TaskManager&;
    [[nodiscard]] auto task_manager() const noexcept -> const tasks::TaskManager&;
    [[nodiscard]] auto team_manager() noexcept -> mailbox::TeamLifecycleManager&;
    [[nodiscard]] auto team_manager() const noexcept -> const mailbox::TeamLifecycleManager&;
    [[nodiscard]] auto mailbox() noexcept -> mailbox::Mailbox&;
    [[nodiscard]] auto mailbox() const noexcept -> const mailbox::Mailbox&;
    [[nodiscard]] auto agent_definitions() const noexcept -> const AgentDefinitionRegistry&;

    auto spawn_agent(const tasks::AgentSpawnRequest& request) -> Result<tasks::AgentSpawnResponse>;
    auto spawn_handler() -> tasks::AgentSpawnHandler;

    auto drain_coordinator_mailbox(std::string_view coordinator_id) -> Result<mailbox::WorkerMailboxDrain>;
    auto publish_task_result(std::string_view sender_id,
                             std::string_view recipient_id,
                             std::string_view task_id,
                             std::string result,
                             std::string summary = {}) -> Result<mailbox::MailboxMessage>;

private:
    auto ensure_team_exists(std::string_view team_name) -> Result<void>;

    tasks::TaskManager task_manager_;
    mailbox::TeamLifecycleManager team_manager_;
    mailbox::Mailbox mailbox_;
    AgentDefinitionRegistry agent_definitions_;
    SubprocessBackend subprocess_backend_;
};

auto create_default_runtime(const std::filesystem::path& cwd,
                            AgentDefinitionLoadOptions options = {})
    -> Result<std::unique_ptr<CoordinatorRuntime>>;

} // namespace codeharness::coordinator

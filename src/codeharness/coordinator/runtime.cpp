//==============================================================================
// runtime.cpp — CoordinatorRuntime 实现
//
// 包含了 spawn_agent 的完整流程：
//
//   LLM 调用 "agent" tool
//          ↓
//   AgentTool::execute()
//          ↓
//   spawn_handler (= CoordinatorRuntime::spawn_agent)
//          ↓
//   1. 验证 mode == "local_agent"
//   2. 构建 TeammateSpawnConfig
//   3. resolve_spawn_config() ← 应用 agent definition
//   4. ensure_team_exists()   ← 确保目标团队存在
//   5. subprocess_backend_.spawn() ← 创建子进程任务
//   6. 查询 TaskRecord
//   7. 返回 AgentSpawnResponse
//          ↓
//   CoordinatorRuntime::publish_task_result
//          收集子 agent 的输出 → 封装 <task-notification> XML →
//          发送到 coordinator mailbox
//
// CoordinatorRuntime 的"双重身份"：
//   - 对上层（RuntimeBundle）：它提供 spawn_agent / drain_mailbox / publish
//   - 对下层（子 agent）：它的 mailbox 接收子 agent 发来的结果
//==============================================================================

#include "codeharness/coordinator/runtime.h"

#include "codeharness/coordinator/spawn_config.h"
#include "codeharness/coordinator/task_notification.h"
#include "codeharness/core/strings.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace codeharness::coordinator
{

namespace
{

auto agent_type_for_lookup(const std::optional<std::string>& value) -> std::string_view
{
    if(!value.ok())
    {
        return {};
    }

    return Trim(*value);
}

} // namespace

CoordinatorRuntime::CoordinatorRuntime(std::filesystem::path task_root,
                                       std::filesystem::path team_root,
                                       std::filesystem::path mailbox_root,
                                       AgentDefinitionRegistry agent_definitions)
    : task_manager_{std::move(task_root)}
    , team_manager_{std::move(team_root)}
    , mailbox_{std::move(mailbox_root)}
    , agent_definitions_{std::move(agent_definitions)}
    , subprocess_backend_{task_manager_, team_manager_}
{
}

auto CoordinatorRuntime::task_manager() noexcept -> tasks::TaskManager&
{
    return task_manager_;
}

auto CoordinatorRuntime::task_manager() const noexcept -> const tasks::TaskManager&
{
    return task_manager_;
}

auto CoordinatorRuntime::team_manager() noexcept -> mailbox::TeamLifecycleManager&
{
    return team_manager_;
}

auto CoordinatorRuntime::team_manager() const noexcept -> const mailbox::TeamLifecycleManager&
{
    return team_manager_;
}

auto CoordinatorRuntime::mailbox() noexcept -> mailbox::Mailbox&
{
    return mailbox_;
}

auto CoordinatorRuntime::mailbox() const noexcept -> const mailbox::Mailbox&
{
    return mailbox_;
}

auto CoordinatorRuntime::agent_definitions() const noexcept -> const AgentDefinitionRegistry&
{
    return agent_definitions_;
}

auto CoordinatorRuntime::spawn_agent(const tasks::AgentSpawnRequest& request)
    -> absl::StatusOr<tasks::AgentSpawnResponse>
{
    if (request.mode != "local_agent")
    {
        return fail<tasks::AgentSpawnResponse>(
            absl::InvalidArgumentError ,
            "Invalid mode. Use local_agent.");
    }

    auto config = TeammateSpawnConfig{
        .name = normalized_agent_name(request.subagent_type.value_or(std::string{})),
        .team = normalized_team_name(request.team.value_or(std::string{})),
        .description = request.description,
        .prompt = request.prompt,
        .cwd = request.cwd,
        .command = request.command,
        .argv = request.argv,
        .env = request.env,
        .model = request.model,
    };

    auto resolved = resolve_spawn_config(std::move(config), agent_definitions_, agent_type_for_lookup(request.subagent_type));
    if(!resolved.ok())
    {
        return resolved.status();
    }

    auto ensured = ensure_team_exists(resolved->team);
    if (!ensured)
    {
        return ensured.error();
    }

    auto spawned = subprocess_backend_.spawn(*resolved);
    if (!spawned)
    {
        return spawned.error();
    }

    auto task = task_manager_.get_task(spawned->task_id);
    if(!task.ok())
    {
        return task.status();
    }
    if (!task->has_value())
    {
        return fail<tasks::AgentSpawnResponse>(
            absl::InternalError ,
            "spawned task not found: " + spawned->task_id);
    }

    return tasks::AgentSpawnResponse{
        .agent_id = spawned->agent_id,
        .task_id = spawned->task_id,
        .backend_type = spawned->backend_type,
        .description = request.description,
        .task = std::move(**task),
    };
}

auto CoordinatorRuntime::spawn_handler() -> tasks::AgentSpawnHandler
{
    return [this](const tasks::AgentSpawnRequest& request) {
        return spawn_agent(request);
    };
}

auto CoordinatorRuntime::drain_coordinator_mailbox(std::string_view coordinator_id)
    -> absl::StatusOr<mailbox::WorkerMailboxDrain>
{
    return mailbox::drain_worker_mailbox(mailbox_, coordinator_id);
}

auto CoordinatorRuntime::publish_task_result(std::string_view sender_id,
                                             std::string_view recipient_id,
                                             std::string_view task_id,
                                             std::string result,
                                             std::string summary) -> absl::StatusOr<mailbox::MailboxMessage>
{
    auto task = task_manager_.get_task(task_id);
    if(!task.ok())
    {
        return task.status();
    }
    if (!task->has_value())
    {
        return fail<mailbox::MailboxMessage>(
            absl::InvalidArgumentError ,
            "No task found with ID: " + std::string{task_id});
    }

    auto notification = make_task_notification(**task, std::move(result), std::move(summary));
    auto message = make_task_result_message(std::string{sender_id}, std::string{recipient_id}, notification);
    auto sent = mailbox_.send(std::string{recipient_id}, std::move(message));
    if (!sent)
    {
        return sent.error();
    }

    return sent;
}

auto CoordinatorRuntime::ensure_team_exists(std::string_view team_name) -> absl::Status
{
    auto existing = team_manager_.get_team(team_name);
    if (!existing)
    {
        return existing.error();
    }
    if (existing->has_value())
    {
        return {};
    }

    auto created = team_manager_.create_team(team_name);
    if(!created.ok())
    {
        if (created.status().kind == absl::AlreadyExistsError )
        {
            return {};
        }
        return created.status();
    }

    return {};
}

auto create_default_runtime(const std::filesystem::path& cwd,
                            AgentDefinitionLoadOptions options)
    -> absl::StatusOr<std::unique_ptr<CoordinatorRuntime>>
{
    auto task_root = tasks::default_task_root();
    if (!task_root)
    {
        return task_root.error();
    }

    auto team_root = mailbox::default_teams_root();
    auto mailbox_root = mailbox::default_mailbox_root();

    auto definitions = load_agent_definition_registry(cwd, std::move(options));
    if (!definitions)
    {
        return definitions.error();
    }

    return std::make_unique<CoordinatorRuntime>(
        std::move(*task_root),
        std::move(team_root),
        std::move(mailbox_root),
        std::move(*definitions));
}

} // namespace codeharness::coordinator

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

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

// Minimal local-worker backend. The runtime owns task/team state and ensures
// teams exist; this backend keeps the lower-level constraint that spawn only
// records members for an existing team.
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

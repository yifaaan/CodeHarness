#pragma once

#include "codeharness/tasks/task_manager.h"
#include "codeharness/tools/tool.h"
#include "codeharness/tools/tool_registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::tasks
{

struct AgentSpawnRequest
{
    std::string description;
    std::string prompt;
    std::string mode = "local_agent";
    std::optional<std::string> subagent_type;
    std::optional<std::string> model;
    std::optional<std::string> command;
    std::optional<std::string> team;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
    std::filesystem::path cwd;
};

struct AgentSpawnResponse
{
    std::string agent_id;
    std::string task_id;
    std::string backend_type = "subprocess";
    std::string description;
    TaskRecord task;
};

auto to_json(nlohmann::json& output, const AgentSpawnRequest& request) -> void;
auto from_json(const nlohmann::json& input, AgentSpawnRequest& request) -> void;
auto to_json(nlohmann::json& output, const AgentSpawnResponse& response) -> void;
auto from_json(const nlohmann::json& input, AgentSpawnResponse& response) -> void;

using AgentSpawnHandler = std::function<Result<AgentSpawnResponse>(const AgentSpawnRequest&)>;

class TaskCreateTool final : public Tool
{
public:
    explicit TaskCreateTool(TaskManager& manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

class TaskListTool final : public Tool
{
public:
    explicit TaskListTool(TaskManager& manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

class TaskGetTool final : public Tool
{
public:
    explicit TaskGetTool(TaskManager& manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

class TaskOutputTool final : public Tool
{
public:
    explicit TaskOutputTool(TaskManager& manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

class TaskStopTool final : public Tool
{
public:
    explicit TaskStopTool(TaskManager& manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

class AgentTool final : public Tool
{
public:
    explicit AgentTool(TaskManager& manager);
    AgentTool(TaskManager& manager, AgentSpawnHandler spawn_handler);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
    AgentSpawnHandler spawn_handler_;
};

auto register_task_tools(ToolRegistry& registry, TaskManager& manager) -> void;
auto register_task_tools(ToolRegistry& registry, TaskManager& manager, AgentSpawnHandler spawn_handler) -> void;

} // namespace codeharness::tasks

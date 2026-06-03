#pragma once

#include "codeharness/tasks/task_manager.h"
#include "codeharness/tools/tool.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness::tasks
{

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

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    TaskManager& manager_;
};

auto register_task_tools(ToolRegistry& registry, TaskManager& manager) -> void;

} // namespace codeharness::tasks

//==============================================================================
// task_tools.h — Agent 任务相关的 LLM 工具定义
//
// 架构角色：接口适配层
// 职责：将 TaskManager（面向进程的管理器）适配成 LLM 可以直接调用的
//       tool（Tool 接口的子类）。LLM 通过 tool_use 调用这些工具来创建/
//       查询/停止后台任务。
//
// 工具清单：
//   TaskCreateTool ("task_create")  — 创建后台 shell 或 agent 任务
//   TaskListTool   ("task_list")    — 列出后台任务（可选按状态过滤）
//   TaskGetTool    ("task_get")     — 查询单个任务详情
//   TaskOutputTool ("task_output")  — 读取任务输出日志（尾部 N 字节）
//   TaskStopTool   ("task_stop")    — 停止正在运行的任务
//   AgentTool      ("agent")        — 通过 coordinator 生成子 agent
//
// 设计要点：
//   - AgentSpawnHandler 是 CoordinatorRuntime::spawn_agent 的 std::function
//     包装。它允许 coordinator 层在 agent spawn 时介入（应用 agent
//     definition、注册 team 成员等）。如果没有提供 handler，AgentTool
//     会直接调用 TaskManager::create_agent_task，跳过 coordinator 层。
//   - AgentSpawnRequest/Response 支持 JSON 序列化，这是为了通过 JSON Lines
//     protocol 在 frontend-backend 之间传递 agent 信息。
//   - Tool 子类按 Tool 接口约定 name()/description()/execute()，
//     is_read_only() 标志让权限系统知道某些工具不需要确认。
//==============================================================================

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

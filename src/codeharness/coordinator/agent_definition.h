#pragma once

#include "codeharness/core/error.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//==============================================================================
// agent_definition.h — Agent 定义加载器
//
// 架构角色：配置模型层
// 职责：定义 AgentDefinition 数据结构、加载选项和注册表。
//
// 设计原理：
//   AgentDefinition 描述了一个”agent 角色”的默认配置。它是子 agent 生成时的
//   模板参数来源。用户可以在 Markdown 文件中编写角色定义：
//
//   .agents/reviewer.md:
//     ---
//     name: reviewer
//     description: Review changed C++ files
//     tools: [read_file, grep, glob]
//     disallowed_tools: [bash]
//     model: claude-sonnet-4-6
//     max_turns: 8
//     ---
//     You are a careful C++ reviewer...
//
//   当主 agent 调用 agent tool 并指定 subagent_type=”reviewer” 时，
//   coordinator 会：
//   1. 从 registry 找到 reviewer 定义
//   2. 将其字段作为”默认值”填充到 spawn config 中
//   3. 主 agent 显式传入的参数（如 prompt）会覆盖定义中的默认值
//
// 加载优先级（后注册覆盖先注册）：
//   1. 默认用户目录 ~/.codeharness/agents/（最低优先级）
//      ~/.openharness/agents/
//      ~/.claude/agents/
//      ~/.agents/agents/
//   2. user_agent_dirs（用户指定的额外目录）
//   3. extra_agent_dirs（运行时传递的额外目录）
//   4. project agent 目录（项目级定义，最高优先级）
//      从 cwd 开始向上查找直到 git 根目录
//
//   为什么后注册覆盖先注册？
//     registry 内部用 unordered_map，”同 name 覆盖”是自然行为。
//     用户级代理应该优先于系统级，项目级应该优先于用户级。
//==============================================================================

namespace codeharness::coordinator
{

struct AgentDefinition
{
    std::string name;                   // agent 类型名，也是 registry key，例如 "reviewer"
    std::string description;            // 什么时候应该使用这个 agent
    std::string system_prompt;          // worker 的系统提示词，来自 markdown 正文
    std::vector<std::string> tools;      // 允许工具；空表示未声明，"*" 表示全允许
    std::vector<std::string> disallowed_tools;
    std::optional<std::string> model;    // 模型覆盖；nullopt 表示继承当前默认模型
    std::optional<std::string> effort;   // low/medium/high 或后续 provider 支持的 effort
    std::optional<std::string> permission_mode;
    std::optional<int> max_turns;        // agent loop 最大轮数；缺失则继承默认值
    std::vector<std::string> skills;     // 预加载 skills 名称
    std::vector<std::string> mcp_servers;// 需要暴露给该 agent 的 MCP server 名称
    std::string source;                  // user/project/plugin/builtin 等来源标签
    std::filesystem::path path;          // 定义文件路径；纯字符串解析时为空
    std::filesystem::path base_dir;      // 定义文件所在目录；加载资源时使用
};

struct AgentDefinitionLoadOptions
{
    bool load_default_user_agents = true;
    bool allow_project_agents = true;

    std::vector<std::filesystem::path> user_agent_dirs;
    std::vector<std::filesystem::path> extra_agent_dirs;
    std::vector<std::filesystem::path> project_agent_dirs = {
        ".codeharness/agents",
        ".openharness/agents",
        ".agents/agents",
        ".claude/agents",
    };
};

// AgentDefinitionRegistry —— 按 agent name 查找定义。
//
// 加载函数返回 vector 是为了保留来源顺序；registry 则负责“后注册者覆盖先注册者”。
// 这样 user/extra/project 的覆盖策略很直观：按顺序 register，越靠后的定义优先。
class AgentDefinitionRegistry
{
public:
    auto register_agent(AgentDefinition definition) -> void;
    auto get(std::string_view name) const -> const AgentDefinition*;
    auto list() const -> std::vector<AgentDefinition>;

private:
    std::unordered_map<std::string, std::unique_ptr<AgentDefinition>> by_name_;
};

// 默认用户级 agent 目录。
auto default_user_agent_dirs() -> std::vector<std::filesystem::path>;

// 解析单个 Markdown 字符串；default_name 通常来自文件名 stem。
auto parse_agent_definition_markdown(std::string_view default_name, std::string content, std::string source = {})
    -> AgentDefinition;

// 加载单个 .md agent 定义文件。
auto load_agent_definition_file(const std::filesystem::path& path, std::string source) -> absl::StatusOr<AgentDefinition>;

// 从多个目录直接扫描 *.md agent 定义文件，按路径排序并去重。
auto load_agent_definitions_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> absl::StatusOr<std::vector<AgentDefinition>>;

// 从 cwd 开始向上查找 project agent 目录，最多到 git 根。返回顺序是父目录 → 子目录，
auto discover_project_agent_dirs(const std::filesystem::path& cwd,
                                 std::span<const std::filesystem::path> relative_dirs)
    -> absl::StatusOr<std::vector<std::filesystem::path>>;

// 组合加载：user → extra → project。
auto load_agent_definitions(const std::filesystem::path& cwd, AgentDefinitionLoadOptions options = {})
    -> absl::StatusOr<std::vector<AgentDefinition>>;

// 组合加载并注册到 registry；后加载来源覆盖先加载来源。
auto load_agent_definition_registry(const std::filesystem::path& cwd, AgentDefinitionLoadOptions options = {})
    -> absl::StatusOr<AgentDefinitionRegistry>;

} // namespace codeharness::coordinator

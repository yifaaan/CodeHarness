#pragma once

#include "codeharness/core/result.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// AgentDefinitionLoader —— coordinator/swarm 的角色定义加载器
//
// Coordinator 创建 worker agent 时，需要回答两个问题：
//   1. 这个 worker 是什么角色？例如 general-purpose、reviewer、tester。
//   2. 这个角色应该带哪些系统提示、工具限制、模型偏好和技能？
//
// 本模块只负责“把磁盘上的 Markdown agent 定义解析成结构化数据”。
//
// 文件格式使用 Markdown + YAML frontmatter
//
//   ---
//   name: reviewer
//   description: Review changed C++ files
//   tools: [read_file, grep, glob]
//   disallowed_tools: [bash]
//   model: claude-sonnet-4-6
//   max_turns: 8
//   skills: [review]
//   ---
//   You are a careful C++ reviewer...

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

// 默认用户级 agent 目录。
auto default_user_agent_dirs() -> std::vector<std::filesystem::path>;

// 解析单个 Markdown 字符串；default_name 通常来自文件名 stem。
auto parse_agent_definition_markdown(std::string_view default_name, std::string content, std::string source = {})
    -> AgentDefinition;

// 加载单个 .md agent 定义文件。
auto load_agent_definition_file(const std::filesystem::path& path, std::string source) -> Result<AgentDefinition>;

// 从多个目录直接扫描 *.md agent 定义文件，按路径排序并去重。
auto load_agent_definitions_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> Result<std::vector<AgentDefinition>>;

// 从 cwd 开始向上查找 project agent 目录，最多到 git 根。返回顺序是父目录 → 子目录，
auto discover_project_agent_dirs(const std::filesystem::path& cwd,
                                 std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>;

// 组合加载：user → extra → project。
auto load_agent_definitions(const std::filesystem::path& cwd, AgentDefinitionLoadOptions options = {})
    -> Result<std::vector<AgentDefinition>>;

} // namespace codeharness::coordinator

#pragma once

#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 团队生命周期管理（TeamLifecycleManager）

// TeamLifecycleManager 的工作是：
//   1. 在磁盘上创建/删除团队目录
//   2. 记录团队有哪些成员（Agent）
//   3. 持久化团队元数据（名字、描述、成员列表、创建时间等）
//
// 【磁盘上的目录结构】
//
//   root/                           <-- TeamLifecycleManager 的根目录
//     my-team/                      <-- 团队名称作为目录名
//       team.json                   <-- 团队元数据（见下方 JSON 格式）
//     another-team/
//       team.json
//
// 【team.json 的格式】
//
//   {
//     "name": "my-team",
//     "description": "A team for analyzing code",
//     "created_at": "2026-06-04T10:00:00Z",
//     "lead_agent_id": "coordinator@my-team",
//     "members": {
//       "coordinator@my-team": {
//         "agent_id": "coordinator@my-team",
//         "name": "coordinator",
//         "backend_type": "subprocess",
//         "joined_at": "2026-06-04T10:00:01Z"
//       },
//       "worker1@my-team": {
//         "agent_id": "worker1@my-team",
//         "name": "worker1",
//         "backend_type": "subprocess",
//         "joined_at": "2026-06-04T10:00:02Z"
//       }
//     }
//   }

namespace codeharness::mailbox
{

// TeamMember —— 团队中的一个成员 Agent
//
// 每个成员对应一个正在运行的 Agent 子进程。agent_id 是全局唯一标识，
// 格式是 "agentName@teamName"（如 "coordinator@my-team"）。
struct TeamMember
{
    std::string agent_id;      // 全局唯一标识，格式 "name@team"
    std::string name;          // 简短名称（如 "coordinator"、"worker1"）
    std::string backend_type;  // 执行后端类型（"subprocess"、"in_process" 等）
    std::string joined_at;     // 加入团队的 ISO 8601 时间戳
};

auto to_json(nlohmann::json& output, const TeamMember& member) -> void;
auto from_json(const nlohmann::json& input, TeamMember& member) -> void;

// TeamFile —— 团队的完整元数据
//
// 持久化为 team.json 文件。每次修改都通过原子写入
struct TeamFile
{
    std::string name;                           // 团队名称(目录名)
    std::string description;                    
    std::string created_at;                     // 创建时间 ISO 8601
    std::string lead_agent_id;                  // Coordinator 的 agent_id
    std::map<std::string, TeamMember> members;  // agent_id → TeamMember
};

auto to_json(nlohmann::json& output, const TeamFile& file) -> void;
auto from_json(const nlohmann::json& input, TeamFile& file) -> void;


// TeamLifecycleManager —— 团队生命周期管理器
class TeamLifecycleManager
{
public:
    // 指定根目录。团队数据存储在 root / <team-name> / team.json。
    explicit TeamLifecycleManager(std::filesystem::path root);

    // 创建一个新团队。
    //
    //   1. 检查 root / name / team.json 是否已存在（防止重复创建）
    //   2. 创建 root / name / 目录
    //   3. 原子写入 team.json
    auto create_team(std::string_view name, std::string_view description = {}) -> Result<TeamFile>;

    // 删除一个团队及其整个目录。
    auto delete_team(std::string_view name) -> Result<void>;

    // 读取团队元数据。
    auto get_team(std::string_view name) const -> Result<std::optional<TeamFile>>;

    // 列出所有团队，按名称排序。
    auto list_teams() const -> Result<std::vector<TeamFile>>;


    // 向团队添加一个成员。
    //
    //   1. 从磁盘读取 team.json
    //   2. 将 member 插入 members map（如果 agent_id 已存在则替换）
    //   3. 原子写回 team.json
    auto add_member(std::string_view team_name, TeamMember member) -> Result<TeamFile>;
    auto remove_member(std::string_view team_name, std::string_view agent_id) -> Result<TeamFile>;


    // 设置 leader（Coordinator Agent）。
    auto set_lead_agent(std::string_view team_name, std::string_view agent_id) -> Result<TeamFile>;


    // 返回根目录路径。
    [[nodiscard]] auto root() const -> const std::filesystem::path&;

    // 返回团队目录路径：root / team_name
    [[nodiscard]] auto team_dir(std::string_view team_name) const -> std::filesystem::path;

private:
    // 从磁盘读取 team.json，如果不存在则返回错误。
    auto require_team(std::string_view team_name) const -> Result<TeamFile>;

    // 原子写入 team.json 到团队目录。
    auto save_team(const TeamFile& team) const -> Result<void>;

    std::filesystem::path root_;
};

// 返回默认的团队根目录路径：~/.codeharness/data/teams
auto default_teams_root() -> std::filesystem::path;

// 将名称中的非字母数字字符替换为连字符，并转为小写。
//   "My Team"        → "my-team"
//   "worker@v2"      → "worker-v2"
//   "test/../../etc"  → "test------etc"
auto sanitize_team_name(std::string_view name) -> std::string;

// 检查名称是否可以作为团队名使用（非空、无路径分隔符、无 . 或 ..）。
auto is_valid_team_name(std::string_view name) -> bool;

} // namespace codeharness::mailbox

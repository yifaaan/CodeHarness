//==============================================================================
// spawn_config.h — Agent 生成配置解析
//
// 架构角色：配置转换层
// 职责：将 AgentSpawnRequest（来自 LLM tool 请求）解析为 TeammateSpawnConfig
//       （subprocess_backend 可消费的配置），并应用 AgentDefinition 模板。
//
// 关键函数：
//   resolve_spawn_config：核心函数。流程：
//     1. 没有 agent_type → 直接返回 raw config（直接使用参数中的值）
//     2. 有 agent_type → 从 registry 查找对应 AgentDefinition
//     3. apply_agent_definition：将 definition 字段作为"默认值"填充到
//        config 中的空字段。细节是"不覆盖"——如果 config 已有值则保留。
//
//   这种"definition 做默认值 + 显式传参覆盖"的设计（类似 Builder 模式）
//   让 agent 定义可以预设大部分字段，同时保留灵活覆盖能力。
//
// 常量：
//   default_agent_name = "agent"
//   default_team_name = "default"
//   make_agent_id → 格式: "name@team"（如 "code-writer@frontend"）
//==============================================================================

#include "codeharness/coordinator/agent_definition.h"
#include "codeharness/coordinator/subprocess_backend.h"
#include "codeharness/core/result.h"

#include <string>
#include <string_view>

namespace codeharness::coordinator
{

inline constexpr std::string_view default_agent_name = "agent";
inline constexpr std::string_view default_team_name = "default";
auto normalized_agent_name(std::string_view name) -> std::string;
auto normalized_team_name(std::string_view team) -> std::string;
auto make_agent_id(std::string_view name, std::string_view team) -> std::string;

auto apply_agent_definition(TeammateSpawnConfig config, const AgentDefinition& definition)
    -> TeammateSpawnConfig;

auto resolve_spawn_config(TeammateSpawnConfig config,
                          const AgentDefinitionRegistry& registry,
                          std::string_view agent_type)
    -> Result<TeammateSpawnConfig>;

} // namespace codeharness::coordinator

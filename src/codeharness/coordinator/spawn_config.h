#pragma once

#include "codeharness/coordinator/agent_definition.h"
#include "codeharness/coordinator/subprocess_backend.h"
#include "codeharness/core/result.h"

#include <string_view>

namespace codeharness::coordinator
{

// 将磁盘上的 AgentDefinition 应用到一次 spawn 配置上。
// 显式 spawn 配置优先；definition 只补齐缺失的模型、提示词、工具和 metadata 来源信息。
auto apply_agent_definition(TeammateSpawnConfig config, const AgentDefinition& definition)
    -> TeammateSpawnConfig;

// 从 registry 按 agent_type 查找定义并合并。agent_type 为空时返回原 config。
auto resolve_spawn_config(TeammateSpawnConfig config,
                          const AgentDefinitionRegistry& registry,
                          std::string_view agent_type)
    -> Result<TeammateSpawnConfig>;

} // namespace codeharness::coordinator

#pragma once

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

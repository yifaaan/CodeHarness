#pragma once

#include "codeharness/core/message.h"

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <vector>

namespace codeharness
{

auto serialize_anthropic_system(std::span<const Message> messages) -> std::string;
auto serialize_anthropic_messages(std::span<const Message> messages) -> nlohmann::json;
auto serialize_anthropic_tools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions)
    -> nlohmann::json;

} // namespace codeharness

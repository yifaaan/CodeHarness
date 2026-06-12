#pragma once

#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

#include "codeharness/core/message.h"

namespace codeharness {

std::string SerializeAnthropicSystem(std::span<const Message> messages);

nlohmann::json SerializeAnthropicMessages(std::span<const Message> messages);

nlohmann::json SerializeAnthropicTools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions);

}  // namespace codeharness

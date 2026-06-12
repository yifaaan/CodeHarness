#pragma once

#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

#include "codeharness/core/message.h"

namespace codeharness {

nlohmann::json SerializeOpenAIInput(std::span<const Message> messages);

nlohmann::json SerializeOpenAITools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions);

}  // namespace codeharness

#pragma once

#include <string_view>

#include "types.h"

namespace codeharness::llm
{

	ModelCapability GetCapability(std::string_view modelName);

} // namespace codeharness::llm

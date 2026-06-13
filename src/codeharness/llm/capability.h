#pragma once

#include <string_view>

#include "types.h"

namespace codeharness::llm {

ModelCapability GetCapability(std::string_view model_name);

}  // namespace codeharness::llm

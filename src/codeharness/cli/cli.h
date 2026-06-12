#pragma once

#include "codeharness/core/error.h"

namespace codeharness
{

auto run_cli(int argc, char** argv) -> absl::StatusOr<int>;

} // namespace codeharness

#pragma once

#include "codeharness/core/result.h"

namespace codeharness
{

auto run_cli(int argc, char **argv) -> Result<int>;

} // namespace codeharness
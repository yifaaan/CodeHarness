#pragma once

#include "loop_hooks.h"
#include "loop_types.h"

namespace codeharness::engine
{

TurnResult RunTurn(TurnInput input, const LoopHooks& hooks = {});

} // namespace codeharness::engine

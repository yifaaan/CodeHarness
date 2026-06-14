#pragma once

#include "LoopHooks.h"
#include "LoopTypes.h"

namespace codeharness::engine
{

	TurnResult RunTurn(TurnInput input, const LoopHooks &hooks = {});

} // namespace codeharness::engine

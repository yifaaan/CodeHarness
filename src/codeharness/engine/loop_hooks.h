#pragma once

#include <functional>
#include <string_view>

namespace codeharness::engine
{

struct LoopHooks
{
	std::function<void(int)> beforeStep;
	std::function<void(int)> afterStep;
	std::function<bool(std::string_view)> shouldContinueAfterStop;
};

} // namespace codeharness::engine

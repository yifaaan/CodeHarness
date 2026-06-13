#pragma once

#include <functional>
#include <string_view>

namespace codeharness::engine {

struct LoopHooks {
  std::function<void(int)> before_step;
  std::function<void(int)> after_step;
  std::function<bool(std::string_view)> should_continue_after_stop;
};

}  // namespace codeharness::engine

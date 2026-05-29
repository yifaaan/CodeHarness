#pragma once

#include <string>
#include <string_view>

namespace codeharness::tui {

auto run_interactive() -> int;
auto run_exec_agent(std::string_view prompt) -> int;

} // namespace codeharness::tui

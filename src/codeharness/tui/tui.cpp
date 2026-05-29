#include <print>
#include "tui.hpp"

namespace codeharness::tui {

auto run_interactive() -> int {
    std::println("CodeHarness TUI - Interactive Mode");
    return 0;
}

auto run_exec_agent(std::string_view prompt) -> int {
    std::println("CodeHarness TUI - Exec Agent Mode");
    return 0;
}

} // namespace codeharness::tui

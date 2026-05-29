#include <print>
#include "cli.hpp"

namespace codeharness::cli {

auto run() -> int {
    std::println("CodeHarness v{}.{}.{}", 0, 1, 0);
    return 0;
}

} // namespace codeharness::cli

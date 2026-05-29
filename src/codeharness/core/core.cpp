#include "core.hpp"

namespace codeharness::core {

auto Runtime::handle_thread(const ThreadInfo& info) -> bool {
    return true;
}

auto Runtime::handle_prompt(std::string_view prompt) -> std::string {
    return {};
}

auto Runtime::invoke_tool(std::string_view name, std::string_view args) -> std::string {
    return {};
}

} // namespace codeharness::core

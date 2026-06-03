#pragma once

#include <string>
#include <utility>
#include <vector>

namespace codeharness
{

inline auto default_shell_command_prefix() -> std::vector<std::string>
{
#if defined(_WIN32)
    return {"cmd.exe", "/c"};
#else
    return {"/bin/sh", "-c"};
#endif
}

inline auto default_shell_command_argv(std::string command) -> std::vector<std::string>
{
    auto argv = default_shell_command_prefix();
    argv.push_back(std::move(command));
    return argv;
}

} // namespace codeharness

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace codeharness
{

inline auto default_shell_command_argv(std::string command) -> std::vector<std::string>
{
#if defined(_WIN32)
    return {"cmd.exe", "/c", std::move(command)};
#else
    return {"/bin/sh", "-c", std::move(command)};
#endif
}

} // namespace codeharness

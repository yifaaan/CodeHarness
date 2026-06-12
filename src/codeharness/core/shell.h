#pragma once

#include <string>
#include <utility>
#include <vector>

namespace codeharness {

inline std::vector<std::string> DefaultShellCommandArgv(std::string command) {
#if defined(_WIN32)
  return {"cmd.exe", "/c", std::move(command)};
#else
  return {"/bin/sh", "-c", std::move(command)};
#endif
}

}  // namespace codeharness

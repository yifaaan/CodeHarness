#pragma once

#include <cstdint>
#include <string>

namespace codeharness::platform {

struct ExecOptions {
  int timeout_seconds = 600;
  std::string working_directory;  // empty = use platform instance cwd
};

struct ExecResult {
  int exit_status = 0;
  bool timed_out = false;
  std::string stdout_output;
  std::string stderr_output;
};

struct MkdirOptions {
  bool parents = true;
  bool exist_ok = true;
};

}  // namespace codeharness::platform

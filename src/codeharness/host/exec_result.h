#pragma once

#include <filesystem>
#include <string>

namespace codeharness::host {

struct ExecOptions {
  int timeout_seconds = 600;
  std::filesystem::path working_directory;
};

struct ExecResult {
  int exit_status = 0;
  bool timed_out = false;
  std::string stdout_output;
  std::string stderr_output;
};

struct MkdirOptions {
  bool recursive = true;
  bool exist_ok = true;
};

struct GlobOptions {
  bool include_directories = true;
  int max_depth = -1;
  std::size_t max_results = 200;
};

}  // namespace codeharness::host

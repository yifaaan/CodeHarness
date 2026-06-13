#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::host {

struct EnvironmentResult {
  std::string shell_path;
  std::string shell_name;
  bool is_windows = false;
  std::vector<std::string> path_dirs;
};

EnvironmentResult DetectEnvironment();
std::string ProbeShell();

}  // namespace codeharness::host
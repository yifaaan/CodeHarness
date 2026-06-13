#include "environment.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace codeharness::host {

#ifdef _WIN32
namespace {

std::string FindGitOnPath() {
  const char* path_env = std::getenv("PATH");
  if (!path_env) return {};

  std::string path_str(path_env);
  std::vector<std::string> dirs;
  size_t start = 0, end;
  while ((end = path_str.find(';', start)) != std::string::npos) {
    dirs.push_back(path_str.substr(start, end - start));
    start = end + 1;
  }
  dirs.push_back(path_str.substr(start));

  for (const auto& dir : dirs) {
    std::error_code ec;
    auto git_exe = std::filesystem::path(dir) / "git.exe";
    if (std::filesystem::exists(git_exe, ec)) {
      return git_exe.string();
    }
  }
  return {};
}

std::string FindGitInProgramFiles() {
  std::vector<std::string> candidates = {
      "C:\\Program Files\\Git\\bin\\git.exe",
      "C:\\Program Files (x86)\\Git\\bin\\git.exe",
  };

  const char* local_appdata = std::getenv("LOCALAPPDATA");
  if (local_appdata) {
    candidates.push_back(std::string(local_appdata) + "\\Programs\\Git\\bin\\git.exe");
  }

  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  return {};
}

std::string ResolveGitBashPath(const std::string& git_exe) {
  std::string cmd = "\"" + git_exe + "\" --exec-path";
  FILE* pipe = _popen(cmd.c_str(), "r");
  if (!pipe) return {};

  char buffer[4096];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe)) {
    result += buffer;
  }
  _pclose(pipe);

  result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
  result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

  if (result.empty()) return {};

  std::filesystem::path exec_path(result);
  auto git_root = exec_path.parent_path().parent_path().parent_path();
  auto bash_candidate = git_root / "bin" / "bash.exe";
  std::error_code ec;
  if (std::filesystem::exists(bash_candidate, ec)) {
    return bash_candidate.string();
  }

  auto mingw_bash = exec_path.parent_path() / "bash.exe";
  if (std::filesystem::exists(mingw_bash, ec)) {
    return mingw_bash.string();
  }

  return {};
}

}  // namespace
#endif

std::string ProbeShell() {
  const char* kim_shell = std::getenv("KIMI_SHELL_PATH");
  if (kim_shell && kim_shell[0]) {
    std::error_code ec;
    if (std::filesystem::exists(kim_shell, ec)) {
      return kim_shell;
    }
  }

#ifdef _WIN32
  auto git_exe = FindGitOnPath();
  if (git_exe.empty()) {
    git_exe = FindGitInProgramFiles();
  }

  if (!git_exe.empty()) {
    auto bash_path = ResolveGitBashPath(git_exe);
    if (!bash_path.empty()) {
      return bash_path;
    }
  }

  std::vector<std::string> bash_candidates = {
      "C:\\Program Files\\Git\\bin\\bash.exe",
      "C:\\Program Files (x86)\\Git\\bin\\bash.exe",
  };
  const char* local_appdata = std::getenv("LOCALAPPDATA");
  if (local_appdata) {
    bash_candidates.push_back(std::string(local_appdata) + "\\Programs\\Git\\bin\\bash.exe");
  }
  const char* userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    bash_candidates.push_back(std::string(userprofile) + "\\scoop\\apps\\git\\current\\bin\\bash.exe");
  }

  for (const auto& candidate : bash_candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }

  return "cmd.exe";
#else
  std::vector<std::string> candidates = {"/bin/bash", "/usr/bin/bash", "/usr/local/bin/bash", "/bin/sh"};
  for (const auto& candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  return "/bin/sh";
#endif
}

EnvironmentResult DetectEnvironment() {
  EnvironmentResult result;
  result.shell_path = ProbeShell();

  auto shell_filename = std::filesystem::path(result.shell_path).filename().string();
  std::transform(shell_filename.begin(), shell_filename.end(), shell_filename.begin(), ::tolower);
  if (shell_filename == "bash.exe" || shell_filename == "bash") {
    result.shell_name = "bash";
  } else if (shell_filename == "cmd.exe" || shell_filename == "cmd") {
    result.shell_name = "cmd";
  } else {
    result.shell_name = "sh";
  }

#ifdef _WIN32
  result.is_windows = true;
  const char* path_env = std::getenv("PATH");
  if (path_env) {
    std::string path_str(path_env);
    size_t start = 0, end;
    while ((end = path_str.find(';', start)) != std::string::npos) {
      result.path_dirs.push_back(path_str.substr(start, end - start));
      start = end + 1;
    }
    result.path_dirs.push_back(path_str.substr(start));
  }
#else
  result.is_windows = false;
  const char* path_env = std::getenv("PATH");
  if (path_env) {
    std::string path_str(path_env);
    size_t start = 0, end;
    while ((end = path_str.find(':', start)) != std::string::npos) {
      result.path_dirs.push_back(path_str.substr(start, end - start));
      start = end + 1;
    }
    result.path_dirs.push_back(path_str.substr(start));
  }
#endif

  return result;
}

}  // namespace codeharness::host
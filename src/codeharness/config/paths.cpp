#include "codeharness/config/paths.h"

#include <cstdlib>
#include <system_error>

namespace codeharness::config {

namespace {

auto home_dir() -> std::filesystem::path {
#ifdef _WIN32
  const auto* home = std::getenv("USERPROFILE");
#else
  const auto* home = std::getenv("HOME");
#endif
  return home ? std::filesystem::path{home} : std::filesystem::path{};
}

}  // namespace

auto config_dir() -> std::filesystem::path {
  const auto* env_dir = std::getenv("CODEHARNESS_CONFIG_DIR");
  if (env_dir && *env_dir != '\0') {
    return std::filesystem::path{env_dir};
  }
  return home_dir() / ".codeharness";
}

auto data_dir() -> std::filesystem::path {
  const auto* env_dir = std::getenv("CODEHARNESS_DATA_DIR");
  if (env_dir && *env_dir != '\0') {
    return std::filesystem::path{env_dir};
  }
  return config_dir() / "data";
}

auto skills_dir() -> std::filesystem::path { return config_dir() / "skills"; }

auto plugins_dir() -> std::filesystem::path { return config_dir() / "plugins"; }

auto agents_dir() -> std::filesystem::path { return config_dir() / "agents"; }

auto sessions_dir() -> std::filesystem::path { return data_dir() / "sessions"; }

auto memory_dir() -> std::filesystem::path { return data_dir() / "memory"; }

auto tasks_dir() -> std::filesystem::path { return data_dir() / "tasks"; }

auto tool_artifacts_dir() -> std::filesystem::path { return data_dir() / "tool_artifacts"; }

auto config_file() -> std::filesystem::path { return config_dir() / "config.toml"; }

auto credentials_file() -> std::filesystem::path { return config_dir() / "credentials.json"; }

auto find_project_config_dir(const std::filesystem::path& cwd) -> std::optional<std::filesystem::path> {
  // Walk up from cwd, looking for .codeharness/config.toml or .codeharness/ directory.
  auto current = std::filesystem::absolute(cwd);

  while (true) {
    auto candidate = current / ".codeharness";
    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec)) {
      auto config = candidate / "config.toml";
      if (std::filesystem::exists(config, ec)) {
        return candidate;
      }
      return candidate;
    }

    // Stop at filesystem root.
    if (current.has_parent_path() && current.parent_path() == current) {
      break;
    }

    // If we're in a git repo, stop at the repo root.
    auto git_dir = current / ".git";
    if (std::filesystem::is_directory(git_dir, ec) || std::filesystem::is_regular_file(git_dir, ec)) {
      break;
    }

    current = current.parent_path();
  }

  return std::nullopt;
}

}  // namespace codeharness::config

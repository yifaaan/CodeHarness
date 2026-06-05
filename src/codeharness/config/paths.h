#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace codeharness::config
{

// Top-level config directory: defaults to ~/.codeharness.
// Overridable via CODEHARNESS_CONFIG_DIR env var.
auto config_dir() -> std::filesystem::path;

// Data directory: defaults to ~/.codeharness/data.
// Overridable via CODEHARNESS_DATA_DIR env var.
auto data_dir() -> std::filesystem::path;

// Subdirectory helpers — all relative to config_dir() or data_dir().
auto skills_dir() -> std::filesystem::path;          // config_dir() / "skills"
auto plugins_dir() -> std::filesystem::path;         // config_dir() / "plugins"
auto agents_dir() -> std::filesystem::path;          // config_dir() / "agents"
auto sessions_dir() -> std::filesystem::path;        // data_dir() / "sessions"
auto memory_dir() -> std::filesystem::path;          // data_dir() / "memory"
auto tasks_dir() -> std::filesystem::path;           // data_dir() / "tasks"
auto tool_artifacts_dir() -> std::filesystem::path;  // data_dir() / "tool_artifacts"

// JSON file paths.
auto settings_file() -> std::filesystem::path;       // config_dir() / "settings.json"
auto credentials_file() -> std::filesystem::path;    // config_dir() / "credentials.json"

// Project-level config directory detection.
// Looks for .codeharness/settings.json in cwd and ancestor directories up to
// the git repository root (or filesystem root if not in a git repo).
auto find_project_config_dir(const std::filesystem::path& cwd) -> std::optional<std::filesystem::path>;

} // namespace codeharness::config

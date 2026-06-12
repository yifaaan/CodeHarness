#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "codeharness/config/config.h"
#include "codeharness/core/error.h"

namespace codeharness::config {

// ConfigManager handles loading, saving, and validating the CodeHarnessConfig
// from a TOML configuration file at the standard config directory.
class ConfigManager {
 public:
  explicit ConfigManager(std::filesystem::path config_dir);

  // Load config from the default path (config_dir_ / "config.toml").
  absl::StatusOr<CodeHarnessConfig> Load();

  // Load config from an explicit path.
  absl::StatusOr<CodeHarnessConfig> LoadFrom(std::string_view path);

  // Save config to the default path.
  absl::Status Save(const CodeHarnessConfig& config);

  // Save config to an explicit path.
  absl::Status SaveTo(std::string_view path, const CodeHarnessConfig& config);

  // Validate configuration consistency.
  absl::Status Validate(const CodeHarnessConfig& config);

  // Returns the default config file path (config_dir_ / "config.toml").
  std::filesystem::path DefaultConfigPath() const;

 private:
  std::filesystem::path config_dir_;
};

}  // namespace codeharness::config

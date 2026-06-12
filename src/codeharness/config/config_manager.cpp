#include "codeharness/config/config_manager.h"
#include "codeharness/config/paths.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>

namespace codeharness::config {

ConfigManager::ConfigManager(std::filesystem::path config_dir) : config_dir_(std::move(config_dir)) {}

absl::StatusOr<CodeHarnessConfig> ConfigManager::Load() {
  return LoadFrom(DefaultConfigPath().string());
}

absl::StatusOr<CodeHarnessConfig> ConfigManager::LoadFrom(std::string_view path) {
  std::ifstream file(std::string{path});
  if (!file.is_open()) {
    return absl::NotFoundError("config file not found: " + std::string{path});
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  auto toml_content = buffer.str();

  auto config = ParseTomlConfig(toml_content);
  if (!config) return config.status();

  if (config->config_dir.empty()) {
    config->config_dir = config_dir_;
  }
  if (config->data_dir.empty()) {
    config->data_dir = config::data_dir();
  }

  auto validation = Validate(*config);
  if (!validation) return validation;

  spdlog::debug("loaded config from {}", path);
  return config;
}

absl::Status ConfigManager::Save(const CodeHarnessConfig& config) {
  return SaveTo(DefaultConfigPath().string(), config);
}

absl::Status ConfigManager::SaveTo(std::string_view path, const CodeHarnessConfig& config) {
  auto validation = Validate(config);
  if (!validation) return validation;

  auto toml_str = SerializeToToml(config);
  if (!toml_str) return toml_str.status();

  auto tmp_path = std::string{path} + ".tmp";
  {
    std::ofstream file(tmp_path);
    if (!file.is_open()) {
      return absl::InternalError("failed to write config: " + tmp_path);
    }
    file << *toml_str;
  }

  std::error_code ec;
  std::filesystem::rename(tmp_path, std::string{path}, ec);
  if (ec) {
    return absl::InternalError("failed to rename config: " + ec.message());
  }

  spdlog::debug("saved config to {}", path);
  return absl::OkStatus();
}

absl::Status ConfigManager::Validate(const CodeHarnessConfig& config) {
  for (const auto& [name, alias] : config.models) {
    if (alias.provider_ref.empty()) {
      return absl::FailedPreconditionError("model '" + name + "' has empty provider_ref");
    }
    if (config.providers.find(alias.provider_ref) == config.providers.end()) {
      return absl::FailedPreconditionError("model '" + name + "' references unknown provider '" + alias.provider_ref +
                                           "'");
    }
  }

  return absl::OkStatus();
}

std::filesystem::path ConfigManager::DefaultConfigPath() const {
  return config_dir_ / "config.toml";
}

}  // namespace codeharness::config

#include "codeharness/config/provider_manager.h"

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

namespace codeharness::config {

ProviderManager::ProviderManager(const CodeHarnessConfig& config, std::filesystem::path config_dir)
    : config_(config), config_dir_(std::move(config_dir)) {}

absl::StatusOr<ResolvedProviderConfig> ProviderManager::ResolveProviderForModel(std::string_view model_name) {
  // Check [models] aliases first.
  auto model_it = config_.models.find(std::string{model_name});
  if (model_it != config_.models.end()) {
    return ResolveProvider(model_it->second.provider_ref);
  }

  // Fall back to treating model_name as a direct provider name.
  return ResolveProvider(model_name);
}

absl::StatusOr<ResolvedProviderConfig> ProviderManager::ResolveProvider(std::string_view provider_name) {
  auto it = config_.providers.find(std::string{provider_name});
  if (it == config_.providers.end()) {
    return absl::NotFoundError("provider '" + std::string{provider_name} + "' not found in config");
  }

  const auto& pc = it->second;
  ResolvedProviderConfig rpc;
  rpc.type = pc.type;
  rpc.base_url = pc.base_url;
  rpc.extra_headers = pc.extra_headers;

  // Resolve API key using the shared resolve_api_key from credentials.
  if (EnsureCredentialsLoaded()) {
    rpc.api_key = resolve_api_key(pc.api_key, pc.type, *cached_credentials_);
  }

  return rpc;
}

std::vector<std::string> ProviderManager::ListModelNames() const {
  std::vector<std::string> names;
  names.reserve(config_.models.size());
  for (const auto& [name, _] : config_.models) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> ProviderManager::ListProviderNames() const {
  std::vector<std::string> names;
  names.reserve(config_.providers.size());
  for (const auto& [name, _] : config_.providers) {
    names.push_back(name);
  }
  return names;
}

bool ProviderManager::EnsureCredentialsLoaded() {
  if (cached_credentials_.has_value()) {
    return true;
  }

  auto creds = load_credentials(config_dir_);
  if (!creds) {
    spdlog::warn("failed to load credentials: {}", creds.status().message());
    cached_credentials_ = Credentials{};
    return false;
  }

  cached_credentials_ = std::move(*creds);
  return true;
}

}  // namespace codeharness::config

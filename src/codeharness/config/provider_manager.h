#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/config/config.h"
#include "codeharness/config/credentials.h"
#include "codeharness/core/error.h"

namespace codeharness::config {

// Fully resolved provider configuration after model → provider → credential
// resolution.
struct ResolvedProviderConfig {
  std::string type;
  std::string model;
  std::string api_key;
  std::string base_url;
  std::map<std::string, std::string> extra_headers;
};

// ProviderManager resolves model aliases to fully configured provider
// configurations, including API key resolution from credentials.
class ProviderManager {
 public:
  ProviderManager(const CodeHarnessConfig& config, std::filesystem::path config_dir);

  // Resolve a model name (from [models] aliases or direct provider name)
  // to a fully resolved provider config with API key.
  absl::StatusOr<ResolvedProviderConfig> ResolveProviderForModel(std::string_view model_name);

  // Resolve a provider by its [providers] name directly, resolving credentials.
  absl::StatusOr<ResolvedProviderConfig> ResolveProvider(std::string_view provider_name);

  // List all available model names from [models] aliases.
  std::vector<std::string> ListModelNames() const;

  // List all available provider names from [providers].
  std::vector<std::string> ListProviderNames() const;

 private:
  bool EnsureCredentialsLoaded();

  const CodeHarnessConfig& config_;
  std::filesystem::path config_dir_;
  std::optional<Credentials> cached_credentials_;
};

}  // namespace codeharness::config

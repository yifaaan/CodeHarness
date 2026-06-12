#pragma once

#include "codeharness/config/config.h"
#include "codeharness/config/credentials.h"
#include "codeharness/core/error.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace codeharness::config
{

// Options parsed from CLI flags, passed to ConfigLoader for the final
// override layer. Each field has a "not-set" sentinel so the loader can
// distinguish "explicitly set to empty" from "not provided".
struct CliOptions
{
    std::string provider_type;
    std::string model;
    std::string api_key;
    std::string base_url;
    int max_turns = -1; // -1 = not specified
    std::filesystem::path cwd;
    std::string profile; // --profile flag
};

// ConfigLoader merges configuration from up to four sources, in ascending
// priority order:
//   1. Hardcoded defaults
//   2. Config file (~/.codeharness/settings.json)
//   3. Environment variables (CODEHARNESS_*)
//   4. CLI flags (CliOptions)
//
// After merging, the active profile (Settings.active_profile) is resolved:
// named profile fields are promoted to top-level Settings fields and the
// API key is resolved via the profile's auth_source.
class ConfigLoader
{
public:
    ConfigLoader();

    // Load, merge, and resolve Settings from all available sources.
    // This is the main entry point.
    auto load(const CliOptions& cli) -> absl::StatusOr<Settings>;

private:
    // Layer 1: hardcoded defaults
    auto load_defaults() -> Settings;

    // Layer 2: read settings.json (if it exists)
    auto load_file(const std::filesystem::path& path) -> absl::StatusOr<Settings>;

    // Layer 3: environment variable overrides
    void apply_env(Settings& settings);

    // Layer 4: CLI flag overrides (highest priority)
    void apply_cli(Settings& settings, const CliOptions& cli);

    // Resolve active profile: promote profile fields to top-level,
    // resolve API key from auth_source.
    void resolve_active_profile(Settings& settings);

    // Cached credentials (loaded on first access).
    std::optional<Credentials> cached_credentials_;
};

} // namespace codeharness::config

#pragma once

#include "codeharness/core/result.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace codeharness::config
{

// Credentials loaded from ~/.codeharness/credentials.json.
// Kept separate from Settings so it can be loaded/stored with stricter
// permissions and never serialized into settings.json output.
struct Credentials
{
    std::map<std::string, std::string> profile_keys; // profile_name → api_key
};

// Load credentials from ~/.codeharness/credentials.json (or an explicit dir).
// Returns empty Credentials (not an error) if the file does not exist.
auto load_credentials(std::optional<std::filesystem::path> config_dir = std::nullopt) -> Result<Credentials>;

// Resolve an API key from an auth_source string:
//   "env:VAR_NAME"         → read from environment variable
//   "credentials:PROFILE"  → read from credentials.profile_keys["PROFILE"]
//   literal key            → returned as-is
//   empty / unknown prefix → for openai: getenv("OPENAI_API_KEY"),
//                             for anthropic: getenv("ANTHROPIC_API_KEY"),
//                             otherwise: empty string
auto resolve_api_key(std::string_view auth_source,
                     std::string_view provider_type,
                     const Credentials& creds) -> std::string;

// Mask an API key for safe logging:
//   "sk-ant-abc123"     → "sk-ant-***"
//   "sk-proj-abc123def" → "sk-proj-***"
//   short key           → first 4 chars + "***"
//   empty               → "***"
auto mask_api_key(std::string_view key) -> std::string;

} // namespace codeharness::config

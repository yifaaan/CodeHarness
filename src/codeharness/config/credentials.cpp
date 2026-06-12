#include "codeharness/config/credentials.h"
#include "codeharness/config/paths.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace codeharness::config
{

auto load_credentials(std::optional<std::filesystem::path> config_dir) -> absl::StatusOr<Credentials>
{
    auto dir = config_dir.value_or(config::config_dir());
    auto path = dir / "credentials.json";

    std::ifstream file(path);
    if (!file.is_open())
    {
        spdlog::debug("credentials file not found at {}, using empty credentials", path.string());
        return Credentials{};
    }

    try
    {
        auto json = nlohmann::json::parse(file);

        Credentials creds;
        if (json.contains("profiles") && json["profiles"].is_object())
        {
            for (auto& [key, value] : json["profiles"].items())
            {
                if (value.contains("api_key") && value["api_key"].is_string())
                {
                    auto api_key = value["api_key"].get<std::string>();
                    if (!api_key.empty())
                    {
                        creds.profile_keys[key] = std::move(api_key);
                    }
                }
            }
        }

        spdlog::debug("loaded {} credential profile(s) from {}",
                      creds.profile_keys.size(), path.string());
        return creds;
    }
    catch (const nlohmann::json::exception& e)
    {
        return absl::StatusOr<Credentials>(absl::FailedPreconditionError("failed to parse " + path.string()) + ": " + e.what());
    }
}

auto resolve_api_key(std::string_view auth_source,
                     std::string_view provider_type,
                     const Credentials& creds) -> std::string
{
    // "env:VAR_NAME"
    if (auth_source.starts_with("env:"))
    {
        auto var_name = auth_source.substr(4);
        auto* value = std::getenv(std::string{var_name}.c_str());
        if (value != nullptr)
        {
            return std::string{value};
        }
        spdlog::debug("env var {} not set for auth_source", var_name);
        return {};
    }

    // "credentials:PROFILE"
    if (auth_source.starts_with("credentials:"))
    {
        auto profile = auth_source.substr(12);
        auto it = creds.profile_keys.find(std::string{profile});
        if (it != creds.profile_keys.end())
        {
            return it->second;
        }
        spdlog::debug("profile '{}' not found in credentials", profile);
        return {};
    }

    // Non-empty literal → use as-is
    if (!auth_source.empty())
    {
        return std::string{auth_source};
    }

    // Empty → provider-specific env var fallback
    if (provider_type == "openai")
    {
        auto* value = std::getenv("OPENAI_API_KEY");
        return value ? std::string{value} : std::string{};
    }
    if (provider_type == "anthropic")
    {
        auto* value = std::getenv("ANTHROPIC_API_KEY");
        return value ? std::string{value} : std::string{};
    }

    return {};
}

auto mask_api_key(std::string_view key) -> std::string
{
    if (key.empty())
    {
        return "***";
    }
    if (key.size() <= 7)
    {
        return std::string{key.substr(0, 4)} + "***";
    }
    return std::string{key.substr(0, 7)} + "***";
}

} // namespace codeharness::config

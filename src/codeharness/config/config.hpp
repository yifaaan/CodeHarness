#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>

namespace codeharness::config {

enum class ProviderKind {
    Deepseek,
    NvidiaNim,
    Openai,
    Atlascloud,
    WanjieArk,
    Openrouter,
    Novita,
    Fireworks,
    Moonshot,
    Sglang,
    Vllm,
    Ollama
};

struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    std::string default_model;
};

struct Config {
    std::string api_key;
    std::string base_url;
    std::string default_text_model;
    ProviderKind provider{ProviderKind::Deepseek};
    std::unordered_map<std::string, ProviderConfig> providers;
};

class ConfigStore {
public:
    auto load(std::string_view path) -> std::optional<Config>;
    auto save(std::string_view path, const Config& config) -> bool;

private:
    Config config_;
};

} // namespace codeharness::config

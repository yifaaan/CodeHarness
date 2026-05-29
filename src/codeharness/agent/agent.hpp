#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace codeharness::agent {

struct ModelInfo {
    std::string id;
    std::string provider;
    std::vector<std::string> aliases;
    bool supports_tools{false};
    bool supports_reasoning{false};
};

class ModelRegistry {
public:
    ModelRegistry();

    auto resolve_model(std::string_view name) -> std::optional<ModelInfo>;
    auto list_models() const -> const std::vector<ModelInfo>&;

private:
    std::vector<ModelInfo> models_;
};

} // namespace codeharness::agent

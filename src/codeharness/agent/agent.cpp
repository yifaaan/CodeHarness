#include "agent.hpp"

namespace codeharness::agent {

ModelRegistry::ModelRegistry() {
    // TODO: populate built-in model catalog
}

auto ModelRegistry::resolve_model(std::string_view name) -> std::optional<ModelInfo> {
    for (const auto& model : models_) {
        if (model.id == name) return model;
        for (const auto& alias : model.aliases) {
            if (alias == name) return model;
        }
    }
    return std::nullopt;
}

auto ModelRegistry::list_models() const -> const std::vector<ModelInfo>& {
    return models_;
}

} // namespace codeharness::agent

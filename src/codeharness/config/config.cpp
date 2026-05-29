#include "config.hpp"

namespace codeharness::config {

auto ConfigStore::load(std::string_view path) -> std::optional<Config> {
    return config_;
}

auto ConfigStore::save(std::string_view path, const Config& config) -> bool {
    config_ = config;
    return true;
}

} // namespace codeharness::config

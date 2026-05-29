#include "secrets.hpp"

namespace codeharness::secrets {

FileKeyringStore::FileKeyringStore(std::string_view path) : path_(path) {}

auto FileKeyringStore::get(std::string_view key) -> std::optional<std::string> {
    auto it = secrets_.find(std::string(key));
    if (it != secrets_.end()) return it->second;
    return std::nullopt;
}

auto FileKeyringStore::set(std::string_view key, std::string_view value) -> bool {
    secrets_[std::string(key)] = std::string(value);
    return true;
}

auto FileKeyringStore::remove(std::string_view key) -> bool {
    return secrets_.erase(std::string(key)) > 0;
}

auto Secrets::auto_detect() -> std::unique_ptr<KeyringStore> {
    return std::make_unique<FileKeyringStore>("secrets.json");
}

} // namespace codeharness::secrets

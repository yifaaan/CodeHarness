#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <memory>

namespace codeharness::secrets {

class KeyringStore {
public:
    virtual ~KeyringStore() = default;
    virtual auto get(std::string_view key) -> std::optional<std::string> = 0;
    virtual auto set(std::string_view key, std::string_view value) -> bool = 0;
    virtual auto remove(std::string_view key) -> bool = 0;
};

class FileKeyringStore : public KeyringStore {
public:
    explicit FileKeyringStore(std::string_view path);

    auto get(std::string_view key) -> std::optional<std::string> override;
    auto set(std::string_view key, std::string_view value) -> bool override;
    auto remove(std::string_view key) -> bool override;

private:
    std::string path_;
    std::unordered_map<std::string, std::string> secrets_;
};

class Secrets {
public:
    static auto auto_detect() -> std::unique_ptr<KeyringStore>;
};

} // namespace codeharness::secrets

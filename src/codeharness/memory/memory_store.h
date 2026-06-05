#pragma once

#include "codeharness/core/result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::memory
{

struct MemoryMetadata
{
    int schema_version = 1;
    std::string id;
    std::string name;
    std::string description;
    std::string type = "project";
    std::string scope = "project";
    std::string category = "knowledge";
    int importance = 0;
    std::string source = "manual";
    std::string signature;
    std::string created_at;
    std::string updated_at;
    std::optional<int> ttl_days;
    bool disabled = false;
    std::vector<std::string> supersedes;
    std::vector<std::string> tags;
};

struct MemoryHeader
{
    std::filesystem::path path;
    std::filesystem::path relative_path;
    std::string title;
    std::string description;
    std::string body_preview;
    std::filesystem::file_time_type modified_at{};
    MemoryMetadata metadata;
};

struct MemoryEntry
{
    MemoryHeader header;
    std::string body;
};

struct AddMemoryRequest
{
    std::string title;
    std::string body;
    std::string type = "project";
    std::string scope = "project";
    std::string category = "knowledge";
    std::string description;
    std::vector<std::string> tags;
};

struct MemoryScanOptions
{
    bool include_disabled = false;
    bool include_expired = false;
    std::optional<std::size_t> max_files = 50;
};

auto default_memory_root() -> Result<std::filesystem::path>;

auto project_memory_dir(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> Result<std::filesystem::path>;

class MemoryStore
{
public:
    explicit MemoryStore(std::filesystem::path root);

    static auto for_project(const std::filesystem::path& cwd) -> Result<MemoryStore>;
    static auto for_project(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
        -> Result<MemoryStore>;

    [[nodiscard]] auto root() const -> const std::filesystem::path&;

    auto add(const AddMemoryRequest& request) const -> Result<MemoryHeader>;
    auto scan(MemoryScanOptions options = {}) const -> Result<std::vector<MemoryHeader>>;
    auto search(std::string_view query, std::size_t max_results = 5) const -> Result<std::vector<MemoryEntry>>;
    auto read(const MemoryHeader& header) const -> Result<MemoryEntry>;
    auto soft_remove(std::string_view name_or_id) const -> Result<bool>;

private:
    std::filesystem::path root_;
};

} // namespace codeharness::memory

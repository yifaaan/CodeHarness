#include "codeharness/memory/memory_store.h"

#include <date/date.h>
#include <fmt/format.h>
#include <git2.h>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "codeharness/config/paths.h"
#include "codeharness/core/git.h"
#include "codeharness/core/paths.h"
#include "codeharness/core/strings.h"
#include "codeharness/core/time.h"
#include "codeharness/tools/text_file.h"

namespace codeharness::memory
{

namespace
{

constexpr std::string_view kIndexFileName = "MEMORY.md";
constexpr std::string_view kIndexHeading = "# Memory Index\n";
constexpr std::string_view kFrontmatterFields[] = {
    "schema_version",
    "id",
    "name",
    "description",
    "type",
    "scope",
    "category",
    "importance",
    "source",
    "signature",
    "created_at",
    "updated_at",
    "ttl_days",
    "disabled",
    "supersedes",
    "tags",
};

struct SplitMemoryFile
{
    YAML::Node frontmatter;
    std::string body;
};

auto resolve_directory(const std::filesystem::path& path) -> absl::StatusOr<std::filesystem::path>
{
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return fail<std::filesystem::path>(absl::InternalError , "failed to resolve directory: " + error.message());
    }

    if (!std::filesystem::is_directory(resolved, error))
    {
        return fail<std::filesystem::path>(absl::InvalidArgumentError , "path is not a directory: " + path.string());
    }

    return resolved;
}

auto normalize_for_signature(std::string_view text) -> std::string
{
    std::string normalized;
    bool previous_space = true;

    for (const auto character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0)
        {
            if (!previous_space)
            {
                normalized.push_back(' ');
                previous_space = true;
            }
            continue;
        }

        if (std::ispunct(byte) != 0)
        {
            continue;
        }

        normalized.push_back(LowerAscii(character));
        previous_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ')
    {
        normalized.pop_back();
    }

    return normalized;
}

auto compute_signature(std::string_view body, std::string_view type, std::string_view category) -> absl::StatusOr<std::string>
{
    auto payload = normalize_for_signature(body);
    payload += '|';
    payload += normalize_for_signature(type);
    payload += '|';
    payload += normalize_for_signature(category);
    return GitBlobHashHex(payload);
}

auto generate_memory_id() -> std::string
{
    std::array<unsigned int, 4> bytes{};
    std::random_device random;
    std::ranges::generate(bytes, [&random] { return random() & 0xFFU; });

    std::ostringstream suffix;
    for (const auto byte : bytes)
    {
        suffix << std::hex << std::setw(2) << std::setfill('0') << byte;
    }

    auto timestamp = UtcTimestampSeconds();
    std::erase(timestamp, '-');
    std::erase(timestamp, ':');
    std::erase(timestamp, 'Z');
    std::ranges::replace(timestamp, 'T', '-');

    return "mem-" + timestamp + "-" + suffix.str();
}

auto yaml_string(const YAML::Node& node, std::string_view key) -> std::optional<std::string>
{
    const auto value = node[std::string{key}];
    if (!value || !value.IsScalar())
    {
        return std::nullopt;
    }

    try
    {
        auto text = std::string{Trim(value.as<std::string>())};
        if (!text.empty())
        {
            return text;
        }
    }
    catch (const YAML::Exception&)
    {
    }

    return std::nullopt;
}

auto yaml_int(const YAML::Node& node, std::string_view key, int fallback) -> int
{
    const auto value = node[std::string{key}];
    if(!value.ok())
    {
        return fallback;
    }

    try
    {
        return value.as<int>();
    }
    catch (const YAML::Exception&)
    {
        return fallback;
    }
}

auto yaml_optional_int(const YAML::Node& node, std::string_view key) -> std::optional<int>
{
    const auto value = node[std::string{key}];
    if (!value || value.IsNull())
    {
        return std::nullopt;
    }

    try
    {
        return value.as<int>();
    }
    catch (const YAML::Exception&)
    {
        return std::nullopt;
    }
}

auto yaml_bool(const YAML::Node& node, std::string_view key, bool fallback) -> bool
{
    const auto value = node[std::string{key}];
    if(!value.ok())
    {
        return fallback;
    }

    try
    {
        return value.as<bool>();
    }
    catch (const YAML::Exception&)
    {
        return fallback;
    }
}

auto yaml_string_list(const YAML::Node& node, std::string_view key) -> std::vector<std::string>
{
    const auto value = node[std::string{key}];
    if(!value.ok())
    {
        return {};
    }

    if (value.IsScalar())
    {
        auto text = yaml_string(node, key);
        if (text)
        {
            return {*text};
        }
        return {};
    }

    if (!value.IsSequence())
    {
        return {};
    }

    std::vector<std::string> result;
    for (const auto& item : value)
    {
        if (!item.IsScalar())
        {
            continue;
        }

        try
        {
            auto text = std::string{Trim(item.as<std::string>())};
            if (!text.empty())
            {
                result.push_back(std::move(text));
            }
        }
        catch (const YAML::Exception&)
        {
        }
    }

    return result;
}

auto split_memory_file(std::string_view content) -> SplitMemoryFile
{
    auto [first_line, offset] = NextLine(content, 0);
    if (Trim(first_line) != "---")
    {
        return SplitMemoryFile{.body = std::string{content}};
    }

    const auto frontmatter_start = offset;
    while (offset < content.size())
    {
        const auto line_start = offset;
        auto [line, next_offset] = NextLine(content, offset);
        offset = next_offset;

        if (Trim(line) != "---")
        {
            continue;
        }

        YAML::Node frontmatter;
        try
        {
            frontmatter = YAML::Load(std::string{content.substr(frontmatter_start, line_start - frontmatter_start)});
        }
        catch (const YAML::Exception&)
        {
            frontmatter = YAML::Node{};
        }

        return SplitMemoryFile{
            .frontmatter = std::move(frontmatter),
            .body = std::string{content.substr(offset)},
        };
    }

    return SplitMemoryFile{.body = std::string{content.substr(frontmatter_start)}};
}

auto first_content_line(std::string_view body) -> std::string
{
    std::size_t offset = 0;
    while (offset < body.size())
    {
        auto [line, next_offset] = NextLine(body, offset);
        offset = next_offset;
        auto stripped = Trim(line);
        if (stripped.empty() || stripped.starts_with('#') || stripped == "---")
        {
            continue;
        }

        if (stripped.size() > 200)
        {
            stripped = stripped.substr(0, 200);
        }

        return std::string{stripped};
    }

    return {};
}

auto body_preview(std::string_view body, std::string_view description) -> std::string
{
    std::string preview;
    std::size_t offset = 0;
    while (offset < body.size() && preview.size() < 300)
    {
        auto [line, next_offset] = NextLine(body, offset);
        offset = next_offset;

        const auto stripped = Trim(line);
        if (stripped.empty() || stripped.starts_with('#') || stripped == description)
        {
            continue;
        }

        if (!preview.empty())
        {
            preview.push_back(' ');
        }

        preview.append(stripped);
    }

    if (preview.size() > 300)
    {
        preview.resize(300);
    }

    return preview;
}

auto parse_timestamp(std::string_view text) -> std::optional<date::sys_seconds>
{
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z')
    {
        return std::nullopt;
    }

    const auto read_number = [](std::string_view value, std::size_t offset, std::size_t size) -> std::optional<int> {
        auto result = 0;
        for (auto index = offset; index < offset + size; ++index)
        {
            if (index >= value.size() || std::isdigit(static_cast<unsigned char>(value[index])) == 0)
            {
                return std::nullopt;
            }
            result = result * 10 + (value[index] - '0');
        }
        return result;
    };

    const auto year = read_number(text, 0, 4);
    const auto month = read_number(text, 5, 2);
    const auto day = read_number(text, 8, 2);
    const auto hour = read_number(text, 11, 2);
    const auto minute = read_number(text, 14, 2);
    const auto second = read_number(text, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second)
    {
        return std::nullopt;
    }

    const auto ymd =
        date::year{*year} / date::month{static_cast<unsigned>(*month)} / date::day{static_cast<unsigned>(*day)};
    if (!ymd.ok() || *hour < 0 || *hour > 23 || *minute < 0 || *minute > 59 || *second < 0 || *second > 60)
    {
        return std::nullopt;
    }

    return date::sys_days{ymd} + std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

auto is_expired(const MemoryMetadata& metadata) -> bool
{
    if (!metadata.ttl_days || *metadata.ttl_days <= 0)
    {
        return false;
    }

    auto base = parse_timestamp(metadata.updated_at);
    if (!base)
    {
        base = parse_timestamp(metadata.created_at);
    }

    if (!base)
    {
        return false;
    }

    const auto expiry = *base + std::chrono::hours{24 * *metadata.ttl_days};
    return date::floor<std::chrono::seconds>(std::chrono::system_clock::now()) >= expiry;
}

auto metadata_from_yaml(const YAML::Node& frontmatter, const std::filesystem::path& path, std::string_view body)
    -> MemoryMetadata
{
    auto metadata = MemoryMetadata{};
    metadata.schema_version = yaml_int(frontmatter, "schema_version", 1);
    metadata.id = yaml_string(frontmatter, "id").value_or({});
    metadata.name = yaml_string(frontmatter, "name").value_or(path.stem().string());
    metadata.description = yaml_string(frontmatter, "description").value_or(first_content_line(body));
    metadata.type = yaml_string(frontmatter, "type").value_or("project");
    metadata.scope = yaml_string(frontmatter, "scope").value_or("project");
    metadata.category = yaml_string(frontmatter, "category").value_or("knowledge");
    metadata.importance = yaml_int(frontmatter, "importance", 0);
    metadata.source = yaml_string(frontmatter, "source").value_or("manual");
    metadata.signature = yaml_string(frontmatter, "signature").value_or({});
    metadata.created_at = yaml_string(frontmatter, "created_at").value_or({});
    metadata.updated_at = yaml_string(frontmatter, "updated_at").value_or({});
    metadata.ttl_days = yaml_optional_int(frontmatter, "ttl_days");
    metadata.disabled = yaml_bool(frontmatter, "disabled", false);
    metadata.supersedes = yaml_string_list(frontmatter, "supersedes");
    metadata.tags = yaml_string_list(frontmatter, "tags");
    return metadata;
}

auto metadata_to_json(const MemoryMetadata& metadata) -> nlohmann::json
{
    return nlohmann::json{
        {"schema_version", metadata.schema_version},
        {"id", metadata.id},
        {"name", metadata.name},
        {"description", metadata.description},
        {"type", metadata.type},
        {"scope", metadata.scope},
        {"category", metadata.category},
        {"importance", metadata.importance},
        {"source", metadata.source},
        {"signature", metadata.signature},
        {"created_at", metadata.created_at},
        {"updated_at", metadata.updated_at},
        {"ttl_days", metadata.ttl_days ? nlohmann::json{*metadata.ttl_days} : nlohmann::json{}},
        {"disabled", metadata.disabled},
        {"supersedes", metadata.supersedes},
        {"tags", metadata.tags},
    };
}

auto render_yaml_value(const nlohmann::json& value) -> std::string
{
    if (value.is_null())
    {
        return "null";
    }

    if (value.is_boolean())
    {
        return value.get<bool>() ? "true" : "false";
    }

    if (value.is_number_integer())
    {
        return std::to_string(value.get<int>());
    }

    return value.dump();
}

auto render_frontmatter(const MemoryMetadata& metadata) -> std::string
{
    const auto fields = metadata_to_json(metadata);
    std::ostringstream output;
    for (const auto field : kFrontmatterFields)
    {
        output << field << ": " << render_yaml_value(fields.at(std::string{field})) << '\n';
    }

    return output.str();
}

auto render_memory_file(const MemoryMetadata& metadata, std::string_view body) -> std::string
{
    const auto body_start = body.find_first_not_of('\n');
    auto normalized_body =
        body_start == std::string_view::npos ? std::string{} : std::string{body.substr(body_start)};

    if (!normalized_body.empty() && normalized_body.back() != '\n')
    {
        normalized_body.push_back('\n');
    }

    return "---\n" + render_frontmatter(metadata) + "---\n\n" + normalized_body;
}

auto parse_memory_entry(const std::filesystem::path& root, const std::filesystem::path& path) -> absl::StatusOr<MemoryEntry>
{
    auto text = ReadTextFile(path);
    if (!text)
    {
        return text.error();
    }

    auto split = split_memory_file(*text);
    auto metadata = metadata_from_yaml(split.frontmatter, path, split.body);
    if (metadata.description.empty())
    {
        metadata.description = metadata.name;
    }

    auto modified_at = std::filesystem::file_time_type{};
    std::error_code error;
    modified_at = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return absl::StatusOr<MemoryEntry>(absl::InternalError("failed to read memory timestamp: " + error.message()));
    }

    auto relative_path = std::filesystem::relative(path, root, error);
    if (error)
    {
        return absl::StatusOr<MemoryEntry>(absl::InternalError("failed to resolve memory relative path: " + error.message()));
    }

    return MemoryEntry{
        .header =
            MemoryHeader{
                .path = path,
                .relative_path = std::move(relative_path),
                .title = metadata.name,
                .description = metadata.description,
                .body_preview = body_preview(split.body, metadata.description),
                .modified_at = modified_at,
                .metadata = std::move(metadata),
            },
        .body = std::move(split.body),
    };
}

auto candidate_memory_files(const std::filesystem::path& root) -> absl::StatusOr<std::vector<std::filesystem::path>>
{
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        return paths;
    }

    for (const auto& entry : std::filesystem::directory_iterator{root, error})
    {
        if (error)
        {
            return fail<std::vector<std::filesystem::path>>(
                absl::InternalError , "failed to scan memory directory: " + error.message());
        }

        const auto is_regular = entry.is_regular_file(error);
        if (error)
        {
            return fail<std::vector<std::filesystem::path>>(
                absl::InternalError , "failed to inspect memory file: " + error.message());
        }

        if (!is_regular || entry.path().extension() != ".md" || entry.path().filename() == kIndexFileName)
        {
            continue;
        }

        paths.push_back(entry.path());
    }

    std::ranges::sort(paths);
    return paths;
}

auto next_memory_path(const std::filesystem::path& root, std::string_view slug) -> std::filesystem::path
{
    auto path = root / (std::string{slug} + ".md");
    if (!std::filesystem::exists(path))
    {
        return path;
    }

    for (auto index = 2; true; ++index)
    {
        auto candidate = root / fmt::format("{}_{}.md", slug, index);
        if (!std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }
}

auto clean_tags(const std::vector<std::string>& tags) -> std::vector<std::string>
{
    std::vector<std::string> cleaned;
    std::set<std::string> seen;
    for (const auto& tag : tags)
    {
        auto stripped = std::string{Trim(tag)};
        if (stripped.empty() || !seen.emplace(stripped).second)
        {
            continue;
        }

        cleaned.push_back(std::move(stripped));
    }

    return cleaned;
}

auto update_index(const std::filesystem::path& root, const MemoryHeader& header) -> absl::Status
{
    auto entrypoint = root / kIndexFileName;
    std::string index_text;

    if (std::filesystem::exists(entrypoint))
    {
        auto content = ReadTextFile(entrypoint);
        if (!content)
        {
            return content.error();
        }
        index_text = std::move(*content);
    }
    else
    {
        index_text = std::string{kIndexHeading};
    }

    const auto filename = header.relative_path.string();
    std::ostringstream output;
    bool found_existing = false;
    std::size_t offset = 0;
    while (offset < index_text.size())
    {
        auto [line, next_offset] = NextLine(index_text, offset);
        offset = next_offset;
        if (line.find(filename) != std::string_view::npos)
        {
            found_existing = true;
            continue;
        }

        output << line << '\n';
    }

    auto updated_index = output.str();
    if (updated_index.empty())
    {
        updated_index = std::string{kIndexHeading};
    }
    if (!updated_index.empty() && updated_index.back() != '\n')
    {
        updated_index.push_back('\n');
    }
    updated_index += fmt::format("- [{}]({})\n", header.title, filename);

    if (!found_existing || updated_index != index_text)
    {
        auto write_result = AtomicWriteTextFile(entrypoint, updated_index);
        if (!write_result)
        {
            return write_result.error();
        }
    }

    return {};
}

auto remove_from_index(const std::filesystem::path& root, const std::filesystem::path& relative_path) -> absl::Status
{
    const auto entrypoint = root / kIndexFileName;
    if (!std::filesystem::exists(entrypoint))
    {
        return {};
    }

    auto content = ReadTextFile(entrypoint);
    if (!content)
    {
        return content.error();
    }

    std::ostringstream output;
    const auto filename = relative_path.string();
    std::size_t offset = 0;
    while (offset < content->size())
    {
        auto [line, next_offset] = NextLine(*content, offset);
        offset = next_offset;
        if (line.find(filename) == std::string_view::npos)
        {
            output << line << '\n';
        }
    }

    auto write_result = AtomicWriteTextFile(entrypoint, output.str());
    if (!write_result)
    {
        return write_result.error();
    }

    return {};
}

auto search_tokens(std::string_view query) -> std::vector<std::string>
{
    std::vector<std::string> tokens;
    std::set<std::string> seen;
    std::string current;

    const auto flush = [&] {
        if (current.size() >= 3 && seen.emplace(current).second)
        {
            tokens.push_back(current);
        }
        current.clear();
    };

    for (const auto character : query)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '_')
        {
            current.push_back(LowerAscii(character));
            continue;
        }

        flush();
    }
    flush();

    return tokens;
}

auto recency_boost(const MemoryHeader& header) -> double
{
    auto timestamp = parse_timestamp(header.metadata.updated_at);
    if (!timestamp)
    {
        timestamp = parse_timestamp(header.metadata.created_at);
    }

    if (!timestamp)
    {
        return 0.0;
    }

    const auto age = date::floor<std::chrono::seconds>(std::chrono::system_clock::now()) - *timestamp;
    if (age <= std::chrono::hours{24 * 14})
    {
        return 0.3;
    }

    if (age <= std::chrono::hours{24 * 30})
    {
        return 0.1;
    }

    return 0.0;
}

auto contains_token(std::string_view text, const std::string& token) -> bool
{
    auto haystack = std::string{text};
    std::ranges::transform(haystack, haystack.begin(), LowerAscii);
    return haystack.find(token) != std::string::npos;
}

} // namespace

auto default_memory_root() -> absl::StatusOr<std::filesystem::path>
{
    return codeharness::config::data_dir() / "memory";
}

auto project_memory_dir(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> absl::StatusOr<std::filesystem::path>
{
    auto resolved = resolve_directory(cwd);
    if(!resolved.ok())
    {
        return resolved.status();
    }

    auto hash = GitBlobHashHex(resolved->string());
    if (!hash)
    {
        return hash.error();
    }

    const auto project_name = Slugify(resolved->filename().string());
    return memory_root / fmt::format("{}-{}", project_name, hash->substr(0, 12));
}

MemoryStore::MemoryStore(std::filesystem::path root) : root_(std::move(root))
{
}

auto MemoryStore::for_project(const std::filesystem::path& cwd) -> absl::StatusOr<MemoryStore>
{
    auto root = default_memory_root();
    if (!root)
    {
        return root.error();
    }

    return for_project(cwd, *root);
}

auto MemoryStore::for_project(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> absl::StatusOr<MemoryStore>
{
    auto root = project_memory_dir(cwd, memory_root);
    if (!root)
    {
        return root.error();
    }

    return MemoryStore{*root};
}

auto MemoryStore::root() const -> const std::filesystem::path&
{
    return root_;
}

auto MemoryStore::add(const AddMemoryRequest& request) const -> absl::StatusOr<MemoryHeader>
{
    const auto title = std::string{Trim(request.title)};
    auto body = std::string{Trim(request.body)};
    auto type = std::string{Trim(request.type)};
    auto scope = std::string{Trim(request.scope)};
    auto category = std::string{Trim(request.category)};
    if (type.empty())
    {
        type = "project";
    }
    if (scope.empty())
    {
        scope = "project";
    }
    if (category.empty())
    {
        category = "knowledge";
    }

    if (title.empty())
    {
        return absl::StatusOr<MemoryHeader>(absl::InvalidArgumentError("memory title is required"));
    }

    if (body.empty())
    {
        return absl::StatusOr<MemoryHeader>(absl::InvalidArgumentError("memory body is required"));
    }

    body.push_back('\n');

    auto mkdir = EnsureDirectory(root_, "memory directory");
    if (!mkdir)
    {
        return mkdir.error();
    }

    auto signature = compute_signature(body, type, category);
    if (!signature)
    {
        return signature.error();
    }

    auto existing =
        scan(MemoryScanOptions{.include_disabled = true, .include_expired = true, .max_files = std::nullopt});
    if (!existing)
    {
        return existing.error();
    }

    const auto duplicate = std::ranges::find_if(*existing, [&](const auto& header) {
        return header.metadata.signature == *signature;
    });
    const auto has_duplicate = duplicate != existing->end();

    const auto now = UtcTimestampSeconds();
    auto metadata = MemoryMetadata{};
    auto path = has_duplicate ? duplicate->path : next_memory_path(root_, Slugify(title));
    if (has_duplicate)
    {
        metadata.id = duplicate->metadata.id;
        metadata.created_at = duplicate->metadata.created_at;
        metadata.importance = duplicate->metadata.importance;
    }
    else
    {
        metadata.id = generate_memory_id();
        metadata.created_at = now;
    }

    metadata.schema_version = 1;
    metadata.name = title;
    metadata.description = std::string{Trim(request.description)};
    if (metadata.description.empty())
    {
        metadata.description = first_content_line(body);
    }
    if (metadata.description.empty())
    {
        metadata.description = title;
    }
    metadata.type = type;
    metadata.scope = scope;
    metadata.category = category;
    metadata.importance = std::max(metadata.importance, 1);
    metadata.source = "manual";
    metadata.signature = *signature;
    metadata.updated_at = now;
    metadata.disabled = false;
    metadata.tags = clean_tags(request.tags);

    auto write_result = AtomicWriteTextFile(path, render_memory_file(metadata, body));
    if (!write_result)
    {
        return write_result.error();
    }

    auto entry = parse_memory_entry(root_, path);
    if (!entry)
    {
        return entry.error();
    }

    auto index_result = update_index(root_, entry->header);
    if (!index_result)
    {
        return index_result.error();
    }

    return entry->header;
}

auto MemoryStore::scan(MemoryScanOptions options) const -> absl::StatusOr<std::vector<MemoryHeader>>
{
    auto paths = candidate_memory_files(root_);
    if (!paths)
    {
        return paths.error();
    }

    std::vector<MemoryHeader> headers;
    for (const auto& path : *paths)
    {
        auto entry = parse_memory_entry(root_, path);
        if (!entry)
        {
            return entry.error();
        }

        if (entry->header.metadata.disabled && !options.include_disabled)
        {
            continue;
        }

        if (is_expired(entry->header.metadata) && !options.include_expired)
        {
            continue;
        }

        headers.push_back(std::move(entry->header));
    }

    std::ranges::sort(headers, [](const auto& left, const auto& right) {
        if (left.modified_at == right.modified_at)
        {
            return left.path.string() < right.path.string();
        }

        return left.modified_at > right.modified_at;
    });

    if (options.max_files && headers.size() > *options.max_files)
    {
        headers.resize(*options.max_files);
    }

    return headers;
}

auto MemoryStore::search(std::string_view query, std::size_t max_results) const -> absl::StatusOr<std::vector<MemoryEntry>>
{
    const auto tokens = search_tokens(query);
    if (tokens.empty() || max_results == 0)
    {
        return std::vector<MemoryEntry>{};
    }

    auto headers = scan(MemoryScanOptions{.max_files = 100});
    if (!headers)
    {
        return headers.error();
    }

    std::vector<std::pair<double, MemoryHeader>> scored;
    for (auto& header : *headers)
    {
        const auto metadata_text = header.title + " " + header.description;
        const auto body_text = header.body_preview;

        auto meta_hits = 0;
        auto body_hits = 0;
        for (const auto& token : tokens)
        {
            if (contains_token(metadata_text, token))
            {
                ++meta_hits;
            }
            if (contains_token(body_text, token))
            {
                ++body_hits;
            }
        }

        if (meta_hits == 0 && body_hits == 0)
        {
            continue;
        }

        const auto score = static_cast<double>(meta_hits) * 2.0 + static_cast<double>(body_hits) +
                           static_cast<double>(header.metadata.importance) * 0.4 + recency_boost(header);
        scored.emplace_back(score, std::move(header));
    }

    std::ranges::sort(scored, [](const auto& left, const auto& right) {
        if (left.first == right.first)
        {
            return left.second.modified_at > right.second.modified_at;
        }

        return left.first > right.first;
    });

    std::vector<MemoryEntry> results;
    for (const auto& [_, header] : scored)
    {
        if (results.size() >= max_results)
        {
            break;
        }

        auto entry = read(header);
        if (!entry)
        {
            return entry.error();
        }

        results.push_back(std::move(*entry));
    }

    return results;
}

auto MemoryStore::read(const MemoryHeader& header) const -> absl::StatusOr<MemoryEntry>
{
    auto entry = parse_memory_entry(root_, header.path);
    if (!entry)
    {
        return entry.error();
    }

    if (entry->header.metadata.disabled)
    {
        return absl::StatusOr<MemoryEntry>(absl::InvalidArgumentError("memory is disabled: " + header.title));
    }

    if (is_expired(entry->header.metadata))
    {
        return absl::StatusOr<MemoryEntry>(absl::InvalidArgumentError("memory is expired: " + header.title));
    }

    return entry;
}

auto MemoryStore::soft_remove(std::string_view name_or_id) const -> absl::StatusOr<bool>
{
    const auto query = std::string{Trim(name_or_id)};
    if (query.empty())
    {
        return absl::StatusOr<bool>(absl::InvalidArgumentError("memory name or id is required"));
    }

    auto headers =
        scan(MemoryScanOptions{.include_disabled = true, .include_expired = true, .max_files = std::nullopt});
    if (!headers)
    {
        return headers.error();
    }

    auto it = std::ranges::find_if(*headers, [&](const auto& header) {
        return query == header.path.filename().string() || query == header.path.stem().string() ||
               query == header.title || query == header.metadata.id;
    });

    if (it == headers->end() || it->metadata.disabled)
    {
        return false;
    }

    auto entry = parse_memory_entry(root_, it->path);
    if (!entry)
    {
        return entry.error();
    }

    entry->header.metadata.disabled = true;
    entry->header.metadata.updated_at = UtcTimestampSeconds();
    auto write_result = AtomicWriteTextFile(it->path, render_memory_file(entry->header.metadata, entry->body));
    if (!write_result)
    {
        return write_result.error();
    }

    auto index_result = remove_from_index(root_, it->relative_path);
    if (!index_result)
    {
        return index_result.error();
    }

    return true;
}

} // namespace codeharness::memory

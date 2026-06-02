#include "codeharness/memory/memory_store.h"

#include <date/date.h>
#include <fmt/format.h>
#include <git2.h>
#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "codeharness/core/strings.h"
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

struct GitRuntime
{
    GitRuntime()
    {
        git_libgit2_init();
    }

    ~GitRuntime()
    {
        git_libgit2_shutdown();
    }

    GitRuntime(const GitRuntime&) = delete;
    auto operator=(const GitRuntime&) -> GitRuntime& = delete;
};

struct SplitMemoryFile
{
    YAML::Node frontmatter;
    std::string body;
};

auto home_directory() -> std::optional<std::filesystem::path>
{
#ifdef _WIN32
    const auto* home = std::getenv("USERPROFILE");
#else
    const auto* home = std::getenv("HOME");
#endif

    if (home == nullptr || *home == '\0')
    {
        return std::nullopt;
    }

    return std::filesystem::path{home};
}

auto resolve_directory(const std::filesystem::path& path) -> Result<std::filesystem::path>
{
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve directory: " + error.message());
    }

    if (!std::filesystem::is_directory(resolved, error))
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "path is not a directory: " + path.string());
    }

    return resolved;
}

auto git_blob_hash_hex(std::string_view text) -> Result<std::string>
{
    GitRuntime runtime;

    git_oid oid{};
    if (git_odb_hash(&oid, text.data(), text.size(), GIT_OBJECT_BLOB) != 0)
    {
        return fail<std::string>(ErrorKind::Internal, "failed to hash memory content");
    }

    std::array<char, GIT_OID_SHA1_HEXSIZE + 1> buffer{};
    git_oid_tostr(buffer.data(), buffer.size(), &oid);
    return std::string{buffer.data()};
}

auto lower_ascii(char character) -> char
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

auto slugify(std::string_view text) -> std::string
{
    std::string slug;
    bool previous_separator = true;

    for (const auto character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0)
        {
            slug.push_back(lower_ascii(character));
            previous_separator = false;
            continue;
        }

        if (!previous_separator)
        {
            slug.push_back('_');
            previous_separator = true;
        }
    }

    while (!slug.empty() && slug.back() == '_')
    {
        slug.pop_back();
    }

    if (slug.empty())
    {
        return "memory";
    }

    return slug;
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

        normalized.push_back(lower_ascii(character));
        previous_space = false;
    }

    if (!normalized.empty() && normalized.back() == ' ')
    {
        normalized.pop_back();
    }

    return normalized;
}

auto compute_signature(std::string_view body, std::string_view type, std::string_view category) -> Result<std::string>
{
    auto payload = normalize_for_signature(body);
    payload += '|';
    payload += normalize_for_signature(type);
    payload += '|';
    payload += normalize_for_signature(category);
    return git_blob_hash_hex(payload);
}

auto now_timestamp() -> std::string
{
    return date::format("%FT%TZ", date::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
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

    auto timestamp = now_timestamp();
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
        auto text = std::string{trim(value.as<std::string>())};
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
    if (!value)
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
    if (!value)
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
    if (!value)
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
            auto text = std::string{trim(item.as<std::string>())};
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
    auto [first_line, offset] = next_line(content, 0);
    if (trim(first_line) != "---")
    {
        return SplitMemoryFile{.body = std::string{content}};
    }

    const auto frontmatter_start = offset;
    while (offset < content.size())
    {
        const auto line_start = offset;
        auto [line, next_offset] = next_line(content, offset);
        offset = next_offset;

        if (trim(line) != "---")
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
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;
        auto stripped = trim(line);
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
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;

        const auto stripped = trim(line);
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

auto parse_memory_entry(const std::filesystem::path& root, const std::filesystem::path& path) -> Result<MemoryEntry>
{
    auto text = read_text_file(path);
    if (!text)
    {
        return nonstd::make_unexpected(text.error());
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
        return fail<MemoryEntry>(ErrorKind::Io, "failed to read memory timestamp: " + error.message());
    }

    auto relative_path = std::filesystem::relative(path, root, error);
    if (error)
    {
        return fail<MemoryEntry>(ErrorKind::Io, "failed to resolve memory relative path: " + error.message());
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

auto candidate_memory_files(const std::filesystem::path& root) -> Result<std::vector<std::filesystem::path>>
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
                ErrorKind::Io, "failed to scan memory directory: " + error.message());
        }

        const auto is_regular = entry.is_regular_file(error);
        if (error)
        {
            return fail<std::vector<std::filesystem::path>>(
                ErrorKind::Io, "failed to inspect memory file: " + error.message());
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

auto ensure_memory_root(const std::filesystem::path& root) -> Result<void>
{
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return fail<void>(ErrorKind::Io, "failed to create memory directory: " + error.message());
    }

    return {};
}

auto clean_tags(const std::vector<std::string>& tags) -> std::vector<std::string>
{
    std::vector<std::string> cleaned;
    std::set<std::string> seen;
    for (const auto& tag : tags)
    {
        auto stripped = std::string{trim(tag)};
        if (stripped.empty() || !seen.emplace(stripped).second)
        {
            continue;
        }

        cleaned.push_back(std::move(stripped));
    }

    return cleaned;
}

auto update_index(const std::filesystem::path& root, const MemoryHeader& header) -> Result<void>
{
    auto entrypoint = root / kIndexFileName;
    std::string index_text;

    if (std::filesystem::exists(entrypoint))
    {
        auto content = read_text_file(entrypoint);
        if (!content)
        {
            return nonstd::make_unexpected(content.error());
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
        auto [line, next_offset] = next_line(index_text, offset);
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
        auto write_result = atomic_write_text_file(entrypoint, updated_index);
        if (!write_result)
        {
            return nonstd::make_unexpected(write_result.error());
        }
    }

    return {};
}

auto remove_from_index(const std::filesystem::path& root, const std::filesystem::path& relative_path) -> Result<void>
{
    const auto entrypoint = root / kIndexFileName;
    if (!std::filesystem::exists(entrypoint))
    {
        return {};
    }

    auto content = read_text_file(entrypoint);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    std::ostringstream output;
    const auto filename = relative_path.string();
    std::size_t offset = 0;
    while (offset < content->size())
    {
        auto [line, next_offset] = next_line(*content, offset);
        offset = next_offset;
        if (line.find(filename) == std::string_view::npos)
        {
            output << line << '\n';
        }
    }

    auto write_result = atomic_write_text_file(entrypoint, output.str());
    if (!write_result)
    {
        return nonstd::make_unexpected(write_result.error());
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
            current.push_back(lower_ascii(character));
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
    std::ranges::transform(haystack, haystack.begin(), lower_ascii);
    return haystack.find(token) != std::string::npos;
}

} // namespace

auto default_memory_root() -> Result<std::filesystem::path>
{
    const auto home = home_directory();
    if (!home)
    {
        return fail<std::filesystem::path>(ErrorKind::Config, "home directory is not available");
    }

    return *home / ".codeharness" / "data" / "memory";
}

auto project_memory_dir(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> Result<std::filesystem::path>
{
    auto resolved = resolve_directory(cwd);
    if (!resolved)
    {
        return nonstd::make_unexpected(resolved.error());
    }

    auto hash = git_blob_hash_hex(resolved->string());
    if (!hash)
    {
        return nonstd::make_unexpected(hash.error());
    }

    const auto project_name = slugify(resolved->filename().string());
    return memory_root / fmt::format("{}-{}", project_name, hash->substr(0, 12));
}

MemoryStore::MemoryStore(std::filesystem::path root) : root_(std::move(root))
{
}

auto MemoryStore::for_project(const std::filesystem::path& cwd) -> Result<MemoryStore>
{
    auto root = default_memory_root();
    if (!root)
    {
        return nonstd::make_unexpected(root.error());
    }

    return for_project(cwd, *root);
}

auto MemoryStore::for_project(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> Result<MemoryStore>
{
    auto root = project_memory_dir(cwd, memory_root);
    if (!root)
    {
        return nonstd::make_unexpected(root.error());
    }

    return MemoryStore{*root};
}

auto MemoryStore::root() const -> const std::filesystem::path&
{
    return root_;
}

auto MemoryStore::add(const AddMemoryRequest& request) const -> Result<MemoryHeader>
{
    const auto title = std::string{trim(request.title)};
    auto body = std::string{trim(request.body)};
    auto type = std::string{trim(request.type)};
    auto scope = std::string{trim(request.scope)};
    auto category = std::string{trim(request.category)};
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
        return fail<MemoryHeader>(ErrorKind::InvalidArgument, "memory title is required");
    }

    if (body.empty())
    {
        return fail<MemoryHeader>(ErrorKind::InvalidArgument, "memory body is required");
    }

    body.push_back('\n');

    auto mkdir = ensure_memory_root(root_);
    if (!mkdir)
    {
        return nonstd::make_unexpected(mkdir.error());
    }

    auto signature = compute_signature(body, type, category);
    if (!signature)
    {
        return nonstd::make_unexpected(signature.error());
    }

    auto existing =
        scan(MemoryScanOptions{.include_disabled = true, .include_expired = true, .max_files = std::nullopt});
    if (!existing)
    {
        return nonstd::make_unexpected(existing.error());
    }

    const auto duplicate = std::ranges::find_if(*existing, [&](const auto& header) {
        return header.metadata.signature == *signature;
    });
    const auto has_duplicate = duplicate != existing->end();

    const auto now = now_timestamp();
    auto metadata = MemoryMetadata{};
    auto path = has_duplicate ? duplicate->path : next_memory_path(root_, slugify(title));
    if (has_duplicate)
    {
        metadata = duplicate->metadata;
    }
    else
    {
        metadata.id = generate_memory_id();
        metadata.created_at = now;
    }

    if (metadata.id.empty())
    {
        metadata.id = generate_memory_id();
    }
    if (metadata.created_at.empty())
    {
        metadata.created_at = now;
    }

    metadata.schema_version = 1;
    metadata.name = title;
    metadata.description = std::string{trim(request.description)};
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

    auto write_result = atomic_write_text_file(path, render_memory_file(metadata, body));
    if (!write_result)
    {
        return nonstd::make_unexpected(write_result.error());
    }

    auto entry = parse_memory_entry(root_, path);
    if (!entry)
    {
        return nonstd::make_unexpected(entry.error());
    }

    auto index_result = update_index(root_, entry->header);
    if (!index_result)
    {
        return nonstd::make_unexpected(index_result.error());
    }

    return entry->header;
}

auto MemoryStore::scan(MemoryScanOptions options) const -> Result<std::vector<MemoryHeader>>
{
    auto paths = candidate_memory_files(root_);
    if (!paths)
    {
        return nonstd::make_unexpected(paths.error());
    }

    std::vector<MemoryHeader> headers;
    for (const auto& path : *paths)
    {
        auto entry = parse_memory_entry(root_, path);
        if (!entry)
        {
            return nonstd::make_unexpected(entry.error());
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

auto MemoryStore::search(std::string_view query, std::size_t max_results) const -> Result<std::vector<MemoryEntry>>
{
    const auto tokens = search_tokens(query);
    if (tokens.empty() || max_results == 0)
    {
        return std::vector<MemoryEntry>{};
    }

    auto headers = scan(MemoryScanOptions{.max_files = 100});
    if (!headers)
    {
        return nonstd::make_unexpected(headers.error());
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
            return nonstd::make_unexpected(entry.error());
        }

        results.push_back(std::move(*entry));
    }

    return results;
}

auto MemoryStore::read(const MemoryHeader& header) const -> Result<MemoryEntry>
{
    auto entry = parse_memory_entry(root_, header.path);
    if (!entry)
    {
        return nonstd::make_unexpected(entry.error());
    }

    if (entry->header.metadata.disabled)
    {
        return fail<MemoryEntry>(ErrorKind::InvalidArgument, "memory is disabled: " + header.title);
    }

    if (is_expired(entry->header.metadata))
    {
        return fail<MemoryEntry>(ErrorKind::InvalidArgument, "memory is expired: " + header.title);
    }

    return entry;
}

auto MemoryStore::soft_remove(std::string_view name_or_id) const -> Result<bool>
{
    const auto query = std::string{trim(name_or_id)};
    if (query.empty())
    {
        return fail<bool>(ErrorKind::InvalidArgument, "memory name or id is required");
    }

    auto headers =
        scan(MemoryScanOptions{.include_disabled = true, .include_expired = true, .max_files = std::nullopt});
    if (!headers)
    {
        return nonstd::make_unexpected(headers.error());
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
        return nonstd::make_unexpected(entry.error());
    }

    entry->header.metadata.disabled = true;
    entry->header.metadata.updated_at = now_timestamp();
    auto write_result = atomic_write_text_file(it->path, render_memory_file(entry->header.metadata, entry->body));
    if (!write_result)
    {
        return nonstd::make_unexpected(write_result.error());
    }

    auto index_result = remove_from_index(root_, it->relative_path);
    if (!index_result)
    {
        return nonstd::make_unexpected(index_result.error());
    }

    return true;
}

} // namespace codeharness::memory

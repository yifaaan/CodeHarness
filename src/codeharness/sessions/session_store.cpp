#include "codeharness/sessions/session_store.h"
#include "codeharness/config/paths.h"
#include "codeharness/core/error.h"
#include "codeharness/tools/text_file.h"

#include <git2.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <system_error>
#include <utility>

namespace codeharness::sessions
{

namespace
{

// ---------------------------------------------------------------------------
// Slugify: same algorithm as memory_store.cpp
// ---------------------------------------------------------------------------
auto lower_ascii(char c) -> char
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
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

    return slug;
}

// ---------------------------------------------------------------------------
// GitRuntime RAII helper (mirrors memory_store.cpp)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// SHA1 hashing (mirrors memory_store.cpp)
// ---------------------------------------------------------------------------
auto git_blob_hash_hex(std::string_view text) -> Result<std::string>
{
    GitRuntime runtime;

    git_oid oid{};
    if (git_odb_hash(&oid, text.data(), text.size(), GIT_OBJECT_BLOB) != 0)
    {
        return fail<std::string>(ErrorKind::Internal, "failed to hash content");
    }

    std::array<char, GIT_OID_SHA1_HEXSIZE + 1> buffer{};
    git_oid_fmt(buffer.data(), &oid);

    return std::string{buffer.data(), GIT_OID_SHA1_HEXSIZE};
}

// ---------------------------------------------------------------------------
// Resolve an absolute canonical path
// ---------------------------------------------------------------------------
auto resolve_directory(const std::filesystem::path& path) -> Result<std::filesystem::path>
{
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io,
                                           "failed to resolve path: " + error.message());
    }
    return resolved;
}

// ---------------------------------------------------------------------------
// Role <-> string
// ---------------------------------------------------------------------------
auto role_to_string(Role role) -> std::string_view
{
    switch (role)
    {
    case Role::System: return "system";
    case Role::User: return "user";
    case Role::Assistant: return "assistant";
    case Role::Tool: return "tool";
    }
    return "unknown";
}

auto role_from_string(std::string_view text) -> Result<Role>
{
    if (text == "system") return Role::System;
    if (text == "user") return Role::User;
    if (text == "assistant") return Role::Assistant;
    if (text == "tool") return Role::Tool;
    return fail<Role>(ErrorKind::InvalidArgument,
                      "unknown role: " + std::string{text});
}

// ---------------------------------------------------------------------------
// ContentBlock -> JSON
// ---------------------------------------------------------------------------
auto content_block_to_json(const ContentBlock& block) -> nlohmann::json
{
    if (auto* text = std::get_if<TextBlock>(&block))
    {
        return nlohmann::json{
            {"type", "text"},
            {"text", text->text},
        };
    }
    if (auto* tool_use = std::get_if<ToolUseBlock>(&block))
    {
        nlohmann::json input = nlohmann::json::object();
        if (!tool_use->input_json.empty())
        {
            try
            {
                input = nlohmann::json::parse(tool_use->input_json);
            }
            catch (const nlohmann::json::exception&)
            {
                input = tool_use->input_json; // store as raw string if not valid JSON
            }
        }
        return nlohmann::json{
            {"type", "tool_use"},
            {"id", tool_use->id},
            {"name", tool_use->name},
            {"input", input},
        };
    }
    if (auto* result = std::get_if<ToolResultBlock>(&block))
    {
        return nlohmann::json{
            {"type", "tool_result"},
            {"tool_use_id", result->tool_use_id},
            {"content", result->content},
            {"is_error", result->is_error},
        };
    }
    return nlohmann::json{{"type", "unknown"}};
}

// ---------------------------------------------------------------------------
// JSON -> ContentBlock
// ---------------------------------------------------------------------------
auto content_block_from_json(const nlohmann::json& j) -> Result<ContentBlock>
{
    auto type = j.value("type", std::string{});
    if (type == "text")
    {
        return ContentBlock{TextBlock{j.value("text", std::string{})}};
    }
    if (type == "tool_use")
    {
        ToolUseBlock block;
        block.id = j.value("id", std::string{});
        block.name = j.value("name", std::string{});
        if (j.contains("input"))
        {
            block.input_json = j["input"].dump();
        }
        return ContentBlock{std::move(block)};
    }
    if (type == "tool_result")
    {
        ToolResultBlock block;
        block.tool_use_id = j.value("tool_use_id", std::string{});
        block.content = j.value("content", std::string{});
        block.is_error = j.value("is_error", false);
        return ContentBlock{std::move(block)};
    }

    return fail<ContentBlock>(ErrorKind::InvalidArgument,
                              "unknown content block type: " + type);
}

// ---------------------------------------------------------------------------
// Extract summary from the first user message
// ---------------------------------------------------------------------------
auto extract_summary(const std::vector<Message>& messages, int max_chars = 80) -> std::string
{
    for (const auto& msg : messages)
    {
        if (msg.role == Role::User)
        {
            auto text = collect_text(msg);
            if (!text.empty())
            {
                if (static_cast<int>(text.size()) <= max_chars)
                {
                    return text;
                }
                return text.substr(0, static_cast<std::size_t>(max_chars));
            }
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// generate_session_id
// ---------------------------------------------------------------------------
auto generate_session_id() -> std::string
{
    static std::mt19937_64 rng = [] {
        std::random_device rd1, rd2, rd3, rd4;
        std::seed_seq seed{rd1(), rd2(), rd3(), rd4()};
        return std::mt19937_64{seed};
    }();

    const auto value = rng();
    std::array<char, 13> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%012llx",
                  static_cast<unsigned long long>(value));
    return std::string{buffer.data()};
}

// ---------------------------------------------------------------------------
// project_session_dir
// ---------------------------------------------------------------------------
auto project_session_dir(const std::filesystem::path& cwd,
                         const std::filesystem::path& sessions_root) -> Result<std::filesystem::path>
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
    return sessions_root / fmt::format("{}-{}", project_name, hash->substr(0, 12));
}

// ===========================================================================
// JSON serialization
// ===========================================================================

auto message_to_json(const Message& msg) -> nlohmann::json
{
    nlohmann::json j;
    j["role"] = role_to_string(msg.role);

    nlohmann::json content = nlohmann::json::array();
    for (const auto& block : msg.content)
    {
        content.push_back(content_block_to_json(block));
    }
    j["content"] = std::move(content);

    return j;
}

auto message_from_json(const nlohmann::json& data) -> Result<Message>
{
    Message msg;

    auto role_result = role_from_string(data.value("role", std::string{}));
    if (!role_result)
    {
        return nonstd::make_unexpected(role_result.error());
    }
    msg.role = *role_result;

    if (data.contains("content") && data["content"].is_array())
    {
        for (const auto& block_json : data["content"])
        {
            auto block = content_block_from_json(block_json);
            if (!block)
            {
                // Skip unparseable blocks rather than failing the entire message.
                spdlog::warn("skipping unparseable content block: {}",
                             block.error().message);
                continue;
            }
            msg.content.push_back(std::move(*block));
        }
    }

    return msg;
}

auto snapshot_to_json(const SessionSnapshot& snapshot) -> nlohmann::json
{
    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : snapshot.messages)
    {
        messages.push_back(message_to_json(msg));
    }

    nlohmann::json j;
    j["session_id"] = snapshot.session_id;
    j["cwd"] = snapshot.cwd.string();
    j["model"] = snapshot.model;
    j["system_prompt"] = snapshot.system_prompt;
    j["messages"] = std::move(messages);
    j["usage"] = nlohmann::json::object();
    j["tool_metadata"] = snapshot.tool_metadata.is_object()
                             ? snapshot.tool_metadata
                             : nlohmann::json::object();
    j["created_at"] = snapshot.created_at;
    j["summary"] = snapshot.summary;
    j["message_count"] = snapshot.message_count;

    return j;
}

auto snapshot_from_json(const nlohmann::json& data) -> Result<SessionSnapshot>
{
    SessionSnapshot s;
    s.session_id = data.value("session_id", std::string{});
    s.model = data.value("model", std::string{});
    s.system_prompt = data.value("system_prompt", std::string{});
    s.created_at = data.value("created_at", 0.0);
    s.summary = data.value("summary", std::string{});
    s.message_count = data.value("message_count", 0);

    // cwd
    auto cwd_str = data.value("cwd", std::string{});
    if (!cwd_str.empty())
    {
        s.cwd = std::filesystem::path{cwd_str};
    }

    // tool_metadata
    if (data.contains("tool_metadata") && data["tool_metadata"].is_object())
    {
        s.tool_metadata = data["tool_metadata"];
    }

    // messages
    if (data.contains("messages") && data["messages"].is_array())
    {
        for (const auto& msg_json : data["messages"])
        {
            auto msg = message_from_json(msg_json);
            if (!msg)
            {
                return nonstd::make_unexpected(msg.error());
            }
            s.messages.push_back(std::move(*msg));
        }

        // Recompute message_count if not present or mismatched.
        if (s.message_count == 0)
        {
            s.message_count = static_cast<int>(s.messages.size());
        }
    }

    // If summary is empty, extract from first user message.
    if (s.summary.empty())
    {
        s.summary = extract_summary(s.messages);
    }

    return s;
}

// ===========================================================================
// SessionStore implementation
// ===========================================================================

SessionStore::SessionStore(std::filesystem::path root) : root_(std::move(root))
{
}

auto SessionStore::for_project(const std::filesystem::path& cwd) -> Result<SessionStore>
{
    return for_project(cwd, config::sessions_dir());
}

auto SessionStore::for_project(const std::filesystem::path& cwd,
                                const std::filesystem::path& sessions_root) -> Result<SessionStore>
{
    auto dir = project_session_dir(cwd, sessions_root);
    if (!dir)
    {
        return nonstd::make_unexpected(dir.error());
    }

    return SessionStore{std::move(*dir)};
}

auto SessionStore::root() const noexcept -> const std::filesystem::path&
{
    return root_;
}

auto SessionStore::ensure_root() -> Result<void>
{
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
    {
        return fail<void>(ErrorKind::Io,
                          "failed to create session directory: " + error.message());
    }
    return {};
}

auto SessionStore::save(const SessionSnapshot& snapshot) -> Result<std::filesystem::path>
{
    auto ensured = ensure_root();
    if (!ensured)
    {
        return nonstd::make_unexpected(ensured.error());
    }

    auto payload = snapshot_to_json(snapshot);
    auto json_text = payload.dump(2) + "\n";

    // Write latest.json
    auto latest_path = root_ / "latest.json";
    auto written = atomic_write_text_file(latest_path, json_text);
    if (!written)
    {
        return nonstd::make_unexpected(written.error());
    }

    // Write session-<id>.json
    auto session_path = root_ / fmt::format("session-{}.json", snapshot.session_id);
    written = atomic_write_text_file(session_path, json_text);
    if (!written)
    {
        return nonstd::make_unexpected(written.error());
    }

    spdlog::debug("saved session {} ({} messages) to {}",
                  snapshot.session_id, snapshot.message_count, latest_path.string());
    return latest_path;
}

auto SessionStore::load_latest() -> Result<std::optional<SessionSnapshot>>
{
    auto path = root_ / "latest.json";
    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::optional<SessionSnapshot>{};
    }

    try
    {
        auto data = nlohmann::json::parse(file);
        auto snapshot = snapshot_from_json(data);
        if (!snapshot)
        {
            return nonstd::make_unexpected(snapshot.error());
        }
        return std::move(*snapshot);
    }
    catch (const nlohmann::json::exception& e)
    {
        return fail<std::optional<SessionSnapshot>>(
            ErrorKind::Config,
            "failed to parse " + path.string() + ": " + e.what());
    }
}

auto SessionStore::list(int limit) -> Result<std::vector<SessionSnapshot>>
{
    std::vector<SessionSnapshot> results;
    std::map<std::string, bool> seen_ids;

    // Collect named session files, newest first.
    std::vector<std::filesystem::path> session_files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec))
    {
        const auto name = entry.path().filename().string();
        if (name.starts_with("session-") && name.ends_with(".json"))
        {
            session_files.push_back(entry.path());
        }
    }

    std::sort(session_files.begin(), session_files.end(),
              [](const auto& a, const auto& b) {
                  return std::filesystem::last_write_time(a) >
                         std::filesystem::last_write_time(b);
              });

    for (const auto& path : session_files)
    {
        if (static_cast<int>(results.size()) >= limit)
        {
            break;
        }

        try
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                continue;
            }
            auto data = nlohmann::json::parse(file);
            auto sid = data.value("session_id", std::string{});
            if (sid.empty() || seen_ids.contains(sid))
            {
                continue;
            }
            seen_ids[sid] = true;

            SessionSnapshot s;
            s.session_id = sid;
            s.summary = data.value("summary", std::string{});
            s.model = data.value("model", std::string{});
            s.message_count = data.value("message_count", 0);
            s.created_at = data.value("created_at", 0.0);

            // Extract summary from first user message if missing.
            if (s.summary.empty() && data.contains("messages") && data["messages"].is_array())
            {
                auto messages = data["messages"];
                for (const auto& msg : messages)
                {
                    if (msg.value("role", std::string{}) == "user")
                    {
                        std::string text;
                        if (msg.contains("content") && msg["content"].is_array())
                        {
                            for (const auto& block : msg["content"])
                            {
                                if (block.value("type", std::string{}) == "text")
                                {
                                    text += block.value("text", std::string{});
                                }
                            }
                        }
                        if (!text.empty())
                        {
                            s.summary = text.substr(0, 80);
                            break;
                        }
                    }
                }
            }

            results.push_back(std::move(s));
        }
        catch (const nlohmann::json::exception&)
        {
            continue; // skip corrupt files
        }
    }

    // Include latest.json if its session_id hasn't been seen.
    if (static_cast<int>(results.size()) < limit)
    {
        auto latest_path = root_ / "latest.json";
        if (std::filesystem::exists(latest_path, ec))
        {
            try
            {
                std::ifstream file(latest_path);
                if (file.is_open())
                {
                    auto data = nlohmann::json::parse(file);
                    auto sid = data.value("session_id", std::string{});
                    if (!sid.empty() && !seen_ids.contains(sid))
                    {
                        SessionSnapshot s;
                        s.session_id = sid;
                        s.summary = data.value("summary", "(latest session)");
                        s.model = data.value("model", std::string{});
                        s.message_count = data.value("message_count", 0);
                        s.created_at = data.value("created_at", 0.0);
                        results.push_back(std::move(s));
                    }
                }
            }
            catch (const nlohmann::json::exception&)
            {
                // skip
            }
        }
    }

    // Sort by created_at descending
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) {
                  return a.created_at > b.created_at;
              });

    if (static_cast<int>(results.size()) > limit)
    {
        results.resize(static_cast<std::size_t>(limit));
    }

    return results;
}

auto SessionStore::load_by_id(const std::string& session_id) -> Result<std::optional<SessionSnapshot>>
{
    // Try named session file first.
    auto path = root_ / fmt::format("session-{}.json", session_id);
    std::ifstream file(path);
    if (file.is_open())
    {
        try
        {
            auto data = nlohmann::json::parse(file);
            auto snapshot = snapshot_from_json(data);
            if (!snapshot)
            {
                return nonstd::make_unexpected(snapshot.error());
            }
            return std::move(*snapshot);
        }
        catch (const nlohmann::json::exception& e)
        {
            return fail<std::optional<SessionSnapshot>>(
                ErrorKind::Config,
                "failed to parse " + path.string() + ": " + e.what());
        }
    }

    // Fall back to latest.json if session_id matches or is "latest".
    if (session_id == "latest")
    {
        return load_latest();
    }

    auto latest_path = root_ / "latest.json";
    std::ifstream latest_file(latest_path);
    if (latest_file.is_open())
    {
        try
        {
            auto data = nlohmann::json::parse(latest_file);
            auto sid = data.value("session_id", std::string{});
            if (sid == session_id)
            {
                auto snapshot = snapshot_from_json(data);
                if (!snapshot)
                {
                    return nonstd::make_unexpected(snapshot.error());
                }
                return std::move(*snapshot);
            }
        }
        catch (const nlohmann::json::exception&)
        {
            // ignore
        }
    }

    return std::optional<SessionSnapshot>{};
}

auto SessionStore::export_markdown(const std::vector<Message>& messages) -> Result<std::filesystem::path>
{
    auto ensured = ensure_root();
    if (!ensured)
    {
        return nonstd::make_unexpected(ensured.error());
    }

    std::string md = "# CodeHarness Session Transcript\n";

    for (const auto& msg : messages)
    {
        md += "\n## ";
        md += role_to_string(msg.role);
        md += "\n\n";

        for (const auto& block : msg.content)
        {
            if (auto* text = std::get_if<TextBlock>(&block))
            {
                md += text->text;
                md += '\n';
            }
            else if (auto* tool_use = std::get_if<ToolUseBlock>(&block))
            {
                md += "```tool\n";
                md += tool_use->name;
                md += ' ';
                md += tool_use->input_json;
                md += "\n```\n";
            }
            else if (auto* result = std::get_if<ToolResultBlock>(&block))
            {
                md += "```tool-result\n";
                md += result->content;
                md += "\n```\n";
            }
        }
    }

    md += '\n';

    auto path = root_ / "transcript.md";
    auto written = atomic_write_text_file(path, md);
    if (!written)
    {
        return nonstd::make_unexpected(written.error());
    }

    return path;
}

} // namespace codeharness::sessions

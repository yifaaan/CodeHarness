#pragma once

#include "codeharness/core/message.h"
#include "codeharness/core/result.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::sessions
{

// Usage tracking placeholder. Will be populated when providers report token
// usage; currently serializes as an empty object for forward compat.
struct UsageSnapshot
{
};

// A complete session snapshot matching the upstream session_storage.py format.
// JSON round-trip via session_store.cpp serialization.
struct SessionSnapshot
{
    std::string session_id;              // 12-char hex identifier
    std::filesystem::path cwd;           // project absolute path
    std::string model;                   // e.g. "claude-sonnet-4-6"
    std::string system_prompt;           // system prompt text
    std::vector<Message> messages;       // full message history
    UsageSnapshot usage;                 // placeholder for future token tracking
    nlohmann::json tool_metadata;        // placeholder for future tool metadata
    double created_at{};                 // unix timestamp
    std::string summary;                 // first 80 chars of first user message
    int message_count{};                 // messages.size() at save time
};

// Generate a unique 12-char hex session ID (uuid4 hex prefix).
auto generate_session_id() -> std::string;

// Compute the project-specific session directory:
//   <sessions_root>/<slug>-<sha1_prefix[:12]>
// where slug is the cwd directory basename and sha1_prefix is the first 12
// hex characters of the SHA1 hash of the resolved absolute path.
auto project_session_dir(const std::filesystem::path& cwd,
                         const std::filesystem::path& sessions_root) -> Result<std::filesystem::path>;

// Persistent session storage backed by the filesystem.
//
// Snapshots are stored under the project session directory:
//   latest.json           — most recent snapshot (overwritten on each save)
//   session-<id>.json     — one file per unique save
//   transcript.md         — markdown export (generated on demand)
//
// Thread safety: not thread-safe. Configuration is loaded once at startup and
// treated as immutable for the lifetime of the store.
class SessionStore
{
public:
    // Construct with an explicit root directory (must already exist).
    explicit SessionStore(std::filesystem::path root);

    SessionStore(const SessionStore&) = delete;
    auto operator=(const SessionStore&) -> SessionStore& = delete;
    SessionStore(SessionStore&&) = default;
    auto operator=(SessionStore&&) -> SessionStore& = default;

    // Factory: create a store rooted at the default sessions directory
    // (~/.codeharness/data/sessions/<slug>-<hash>).
    static auto for_project(const std::filesystem::path& cwd) -> Result<SessionStore>;

    // Factory: same but with an explicit sessions_root (for testing).
    static auto for_project(const std::filesystem::path& cwd,
                            const std::filesystem::path& sessions_root) -> Result<SessionStore>;

    [[nodiscard]] auto root() const noexcept -> const std::filesystem::path&;

    // Persist a snapshot. Writes both latest.json and session-<id>.json.
    // Returns the path to latest.json.
    auto save(const SessionSnapshot& snapshot) -> Result<std::filesystem::path>;

    // Load the most recent snapshot (from latest.json).
    // Returns nullopt if no snapshot exists.
    auto load_latest() -> Result<std::optional<SessionSnapshot>>;

    // List saved sessions, newest first. Capped by limit.
    auto list(int limit = 20) -> Result<std::vector<SessionSnapshot>>;

    // Load a specific snapshot by session_id. Falls back to latest.json when
    // session_id is "latest".
    auto load_by_id(const std::string& session_id) -> Result<std::optional<SessionSnapshot>>;

    // Export messages as a Markdown transcript. Writes transcript.md and
    // returns the path.
    auto export_markdown(const std::vector<Message>& messages) -> Result<std::filesystem::path>;

private:
    std::filesystem::path root_;

    auto ensure_root() -> Result<void>;
};

// --- JSON serialization (declared for testability) ---

auto snapshot_to_json(const SessionSnapshot& snapshot) -> nlohmann::json;
auto snapshot_from_json(const nlohmann::json& data) -> Result<SessionSnapshot>;

auto message_to_json(const Message& msg) -> nlohmann::json;
auto message_from_json(const nlohmann::json& data) -> Result<Message>;

} // namespace codeharness::sessions

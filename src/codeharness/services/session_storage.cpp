#include "codeharness/services/session_storage.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <absl/time/clock.h>
#include <absl/time/time.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "codeharness/engine/message_json.h"

namespace codeharness::services {
namespace {

    [[nodiscard]] auto to_iso8601(std::chrono::system_clock::time_point time_point)
        -> std::string {
        return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", absl::FromChrono(time_point),
                                absl::UTCTimeZone());
    }

    [[nodiscard]] auto from_iso8601(const std::string& value)
        -> std::chrono::system_clock::time_point {
        auto parsed = absl::Time{};
        auto error = std::string{};
        if (!absl::ParseTime("%Y-%m-%dT%H:%M:%SZ", value, absl::UTCTimeZone(), &parsed,
                             &error)) {
            throw std::invalid_argument{
                absl::StrCat("invalid session timestamp: ", value, ": ", error)};
        }

        return absl::ToChronoTime(parsed);
    }

    [[nodiscard]] auto compact_utc_timestamp(std::chrono::system_clock::time_point time_point)
        -> std::string {
        return absl::FormatTime("%Y%m%d-%H%M%S", absl::FromChrono(time_point),
                                absl::UTCTimeZone());
    }

    [[nodiscard]] auto random_hex_suffix() -> std::string {
        auto random = std::random_device{};
        auto engine = std::mt19937_64{random()};
        const auto value = static_cast<std::uint32_t>(engine());

        return absl::StrFormat("%08x", value);
    }

    [[nodiscard]] auto make_session_id() -> std::string {
        return absl::StrCat(compact_utc_timestamp(absl::ToChronoTime(absl::Now())), "-",
                            random_hex_suffix());
    }

    auto write_session_file(const std::filesystem::path& path,
                            const SessionMetadata& metadata,
                            absl::Span<const engine::ConversationMessage> messages) -> void {
        std::filesystem::create_directories(path.parent_path());

        auto stored_messages =
            std::vector<engine::ConversationMessage>{messages.begin(), messages.end()};
        const auto payload = nlohmann::json{
            {"metadata", metadata},
            {"messages", std::move(stored_messages)},
        };

        auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
        if (!out.is_open()) {
            throw std::runtime_error{
                absl::StrCat("failed to open session file for writing: ", path.string())};
        }

        out << payload.dump(2) << '\n';
    }

    [[nodiscard]] auto read_session_json(const std::filesystem::path& path) -> nlohmann::json {
        auto in = std::ifstream{path, std::ios::binary};
        if (!in.is_open()) {
            throw std::runtime_error{absl::StrCat("failed to open session file: ", path.string())};
        }

        return nlohmann::json::parse(in);
    }

}  // namespace

    auto to_json(nlohmann::json& value, const SessionMetadata& metadata) -> void {
        value = {
            {"id", metadata.id},
            {"name", metadata.name},
            {"model", metadata.model},
            {"cwd", metadata.cwd.string()},
            {"created_at", to_iso8601(metadata.created_at)},
            {"updated_at", to_iso8601(metadata.updated_at)},
        };
    }

    auto from_json(const nlohmann::json& value, SessionMetadata& metadata) -> void {
        metadata = SessionMetadata{
            .id = value.at("id").get<std::string>(),
            .name = value.at("name").get<std::string>(),
            .model = value.at("model").get<std::string>(),
            .cwd = std::filesystem::path{value.at("cwd").get<std::string>()},
            .created_at = from_iso8601(value.at("created_at").get<std::string>()),
            .updated_at = from_iso8601(value.at("updated_at").get<std::string>()),
        };
    }

    auto to_json(nlohmann::json& value, const Session& session) -> void {
        value = {
            {"metadata", session.metadata},
            {"messages", session.messages},
        };
    }

    auto from_json(const nlohmann::json& value, Session& session) -> void {
        session = Session{
            .metadata = value.at("metadata").get<SessionMetadata>(),
            .messages = value.at("messages").get<std::vector<engine::ConversationMessage>>(),
        };
    }

    SessionStorage::SessionStorage(std::filesystem::path root_dir)
        : root_dir_{std::move(root_dir)} {
        std::filesystem::create_directories(root_dir_);
    }

    auto SessionStorage::create_session(std::string name,
                                        std::string model,
                                        std::filesystem::path cwd) -> SessionMetadata {
        const auto now = absl::Now();
        auto metadata = SessionMetadata{
            .id = make_session_id(),
            .name = std::move(name),
            .model = std::move(model),
            .cwd = std::move(cwd),
            .created_at = absl::ToChronoTime(now),
            .updated_at = absl::ToChronoTime(now),
        };

        write_session_file(session_path(metadata.id), metadata, {});
        return metadata;
    }

    auto SessionStorage::save_messages(
        absl::string_view session_id,
        absl::Span<const engine::ConversationMessage> messages) -> void {
        const auto path = session_path(session_id);
        auto session = load_session(session_id);
        session.metadata.updated_at = absl::ToChronoTime(absl::Now());
        write_session_file(path, session.metadata, messages);
    }

    auto SessionStorage::load_session(absl::string_view session_id) const -> Session {
        const auto payload = read_session_json(session_path(session_id));
        return payload.get<Session>();
    }

    auto SessionStorage::list_sessions() const -> std::vector<SessionMetadata> {
        auto result = std::vector<SessionMetadata>{};
        if (!std::filesystem::exists(root_dir_)) {
            return result;
        }

        for (const auto& entry : std::filesystem::directory_iterator{root_dir_}) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            try {
                const auto payload = read_session_json(entry.path());
                result.push_back(payload.at("metadata").get<SessionMetadata>());
            } catch (const std::exception&) {
                continue;
            }
        }

        std::ranges::sort(result, [](const auto& lhs, const auto& rhs) {
            return lhs.updated_at > rhs.updated_at;
        });
        return result;
    }

    auto SessionStorage::session_path(absl::string_view session_id) const
        -> std::filesystem::path {
        return root_dir_ / absl::StrCat(session_id, ".json");
    }

}  // namespace codeharness::services

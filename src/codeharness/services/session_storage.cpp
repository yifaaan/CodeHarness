#include "codeharness/services/session_storage.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
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
#include <string>
#include <utility>
#include <vector>

#include "codeharness/engine/message_json.h"
#include "codeharness/logging.h"

namespace codeharness::services {
namespace {

    [[nodiscard]] auto to_iso8601(std::chrono::system_clock::time_point time_point)
        -> std::string {
        return absl::FormatTime("%Y-%m-%dT%H:%M:%SZ", absl::FromChrono(time_point),
                                absl::UTCTimeZone());
    }

    [[nodiscard]] auto from_iso8601(const std::string& value)
        -> absl::StatusOr<std::chrono::system_clock::time_point> {
        auto parsed = absl::Time{};
        auto error = std::string{};
        if (!absl::ParseTime("%Y-%m-%dT%H:%M:%SZ", value, absl::UTCTimeZone(), &parsed,
                             &error)) {
            return absl::InvalidArgumentError(
                absl::StrCat("invalid session timestamp: ", value, ": ", error));
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
                            absl::Span<const engine::ConversationMessage> messages)
        -> absl::Status {
        std::filesystem::create_directories(path.parent_path());

        auto stored_messages =
            std::vector<engine::ConversationMessage>{messages.begin(), messages.end()};
        const auto payload = nlohmann::json{
            {"metadata", metadata},
            {"messages", std::move(stored_messages)},
        };

        auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
        if (!out.is_open()) {
            return absl::InternalError(
                absl::StrCat("failed to open session file for writing: ", path.string()));
        }

        out << payload.dump(2) << '\n';
        return absl::OkStatus();
    }

    [[nodiscard]] auto read_session_json(const std::filesystem::path& path)
        -> absl::StatusOr<nlohmann::json> {
        auto in = std::ifstream{path, std::ios::binary};
        if (!in.is_open()) {
            return absl::NotFoundError(
                absl::StrCat("failed to open session file: ", path.string()));
        }

        try {
            return nlohmann::json::parse(in);
        } catch (const nlohmann::json::parse_error& error) {
            return absl::InvalidArgumentError(
                absl::StrCat("failed to parse session file ", path.string(), ": ", error.what()));
        }
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

    auto session_metadata_from_json(const nlohmann::json& value)
        -> absl::StatusOr<SessionMetadata> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("session metadata must be a JSON object");
        }

        if (!value.contains("id") || !value.at("id").is_string()) {
            return absl::InvalidArgumentError("session metadata is missing string field: id");
        }
        if (!value.contains("name") || !value.at("name").is_string()) {
            return absl::InvalidArgumentError("session metadata is missing string field: name");
        }
        if (!value.contains("model") || !value.at("model").is_string()) {
            return absl::InvalidArgumentError("session metadata is missing string field: model");
        }
        if (!value.contains("cwd") || !value.at("cwd").is_string()) {
            return absl::InvalidArgumentError("session metadata is missing string field: cwd");
        }
        if (!value.contains("created_at") || !value.at("created_at").is_string()) {
            return absl::InvalidArgumentError(
                "session metadata is missing string field: created_at");
        }
        if (!value.contains("updated_at") || !value.at("updated_at").is_string()) {
            return absl::InvalidArgumentError(
                "session metadata is missing string field: updated_at");
        }

        auto created_at = from_iso8601(value.at("created_at").get<std::string>());
        if (!created_at.ok()) {
            return created_at.status();
        }

        auto updated_at = from_iso8601(value.at("updated_at").get<std::string>());
        if (!updated_at.ok()) {
            return updated_at.status();
        }

        return SessionMetadata{
            .id = value.at("id").get<std::string>(),
            .name = value.at("name").get<std::string>(),
            .model = value.at("model").get<std::string>(),
            .cwd = std::filesystem::path{value.at("cwd").get<std::string>()},
            .created_at = *created_at,
            .updated_at = *updated_at,
        };
    }

    auto from_json(const nlohmann::json& value, SessionMetadata& metadata) -> void {
        auto parsed = session_metadata_from_json(value);
        metadata = parsed.ok() ? std::move(*parsed) : SessionMetadata{};
    }

    auto to_json(nlohmann::json& value, const Session& session) -> void {
        value = {
            {"metadata", session.metadata},
            {"messages", session.messages},
        };
    }

    auto session_from_json(const nlohmann::json& value) -> absl::StatusOr<Session> {
        if (!value.is_object()) {
            return absl::InvalidArgumentError("session must be a JSON object");
        }
        if (!value.contains("metadata")) {
            return absl::InvalidArgumentError("session is missing metadata");
        }
        if (!value.contains("messages") || !value.at("messages").is_array()) {
            return absl::InvalidArgumentError("session is missing messages array");
        }

        auto metadata = session_metadata_from_json(value.at("metadata"));
        if (!metadata.ok()) {
            return metadata.status();
        }

        auto messages = std::vector<engine::ConversationMessage>{};
        messages.reserve(value.at("messages").size());
        for (const auto& message_value : value.at("messages")) {
            auto message = engine::conversation_message_from_json(message_value);
            if (!message.ok()) {
                return message.status();
            }
            messages.push_back(std::move(*message));
        }

        return Session{
            .metadata = std::move(*metadata),
            .messages = std::move(messages),
        };
    }

    auto from_json(const nlohmann::json& value, Session& session) -> void {
        auto parsed = session_from_json(value);
        session = parsed.ok() ? std::move(*parsed) : Session{};
    }

    SessionStorage::SessionStorage(std::filesystem::path root_dir)
        : root_dir_{std::move(root_dir)} {
        std::filesystem::create_directories(root_dir_);
    }

    auto SessionStorage::create_session(std::string name,
                                        std::string model,
                                        std::filesystem::path cwd)
        -> absl::StatusOr<SessionMetadata> {
        const auto now = absl::Now();
        auto metadata = SessionMetadata{
            .id = make_session_id(),
            .name = std::move(name),
            .model = std::move(model),
            .cwd = std::move(cwd),
            .created_at = absl::ToChronoTime(now),
            .updated_at = absl::ToChronoTime(now),
        };

        auto status = write_session_file(session_path(metadata.id), metadata, {});
        if (!status.ok()) {
            return status;
        }
        return metadata;
    }

    auto SessionStorage::save_messages(
        absl::string_view session_id,
        absl::Span<const engine::ConversationMessage> messages) -> absl::Status {
        const auto path = session_path(session_id);
        auto session = load_session(session_id);
        if (!session.ok()) {
            return session.status();
        }
        session->metadata.updated_at = absl::ToChronoTime(absl::Now());
        return write_session_file(path, session->metadata, messages);
    }

    auto SessionStorage::load_session(absl::string_view session_id) const -> absl::StatusOr<Session> {
        auto payload = read_session_json(session_path(session_id));
        if (!payload.ok()) {
            return payload.status();
        }
        return session_from_json(*payload);
    }

    auto SessionStorage::list_sessions() const -> absl::StatusOr<std::vector<SessionMetadata>> {
        auto result = std::vector<SessionMetadata>{};
        if (!std::filesystem::exists(root_dir_)) {
            return result;
        }

        for (const auto& entry : std::filesystem::directory_iterator{root_dir_}) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            auto payload = read_session_json(entry.path());
            if (!payload.ok()) {
                CH_LOG_WARN("SessionStorage::list_sessions",
                            "skipping unreadable session file path={} status={}",
                            entry.path().string(), payload.status().message());
                continue;
            }

            if (!payload->contains("metadata")) {
                CH_LOG_WARN("SessionStorage::list_sessions",
                            "skipping session without metadata path={}",
                            entry.path().string());
                continue;
            }

            auto metadata = session_metadata_from_json(payload->at("metadata"));
            if (!metadata.ok()) {
                CH_LOG_WARN("SessionStorage::list_sessions",
                            "skipping invalid session metadata path={} status={}",
                            entry.path().string(), metadata.status().message());
                continue;
            }
            result.push_back(std::move(*metadata));
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

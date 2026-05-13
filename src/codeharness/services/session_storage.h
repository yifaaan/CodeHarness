#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>
#include <absl/types/span.h>

#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "codeharness/engine/message_json.h"

namespace codeharness::services {

    struct SessionMetadata {
        std::string id;
        std::string name;
        std::string model;
        std::filesystem::path cwd;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point updated_at;
    };

    struct Session {
        SessionMetadata metadata;
        std::vector<engine::ConversationMessage> messages;
    };

    auto to_json(nlohmann::json& value, const SessionMetadata& metadata) -> void;
    auto from_json(const nlohmann::json& value, SessionMetadata& metadata) -> void;
    auto to_json(nlohmann::json& value, const Session& session) -> void;
    auto from_json(const nlohmann::json& value, Session& session) -> void;
    [[nodiscard]] auto session_metadata_from_json(const nlohmann::json& value)
        -> absl::StatusOr<SessionMetadata>;
    [[nodiscard]] auto session_from_json(const nlohmann::json& value) -> absl::StatusOr<Session>;

    class SessionStorage {
    public:
        explicit SessionStorage(std::filesystem::path root_dir);

        [[nodiscard]] auto create_session(std::string name,
                                          std::string model,
                                          std::filesystem::path cwd)
            -> absl::StatusOr<SessionMetadata>;

        auto save_messages(absl::string_view session_id,
                           absl::Span<const engine::ConversationMessage> messages)
            -> absl::Status;

        [[nodiscard]] auto load_session(absl::string_view session_id) const
            -> absl::StatusOr<Session>;
        [[nodiscard]] auto list_sessions() const -> absl::StatusOr<std::vector<SessionMetadata>>;

    private:
        [[nodiscard]] auto session_path(absl::string_view session_id) const
            -> std::filesystem::path;

        std::filesystem::path root_dir_;
    };

}  // namespace codeharness::services

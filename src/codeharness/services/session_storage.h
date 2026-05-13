#pragma once

#include <absl/strings/string_view.h>
#include <absl/types/span.h>

#include <string>

#include "codeharness/engine/message.h"

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

    class SessionStorage {
    public:
        explicit SessionStorage(std::filesystem::path root_dir);

        [[nodiscard]] auto create_session(std::string name,
                                          std::string model,
                                          std::filesystem::path cwd) -> SessionMetadata;

        auto save_messages(absl::string_view session_id,
                           absl::Span<const engine::ConversationMessage> messages) -> void;

        [[nodiscard]] auto load_session(absl::string_view session_id) const -> Session;
        [[nodiscard]] auto list_sessions() const -> std::vector<SessionMetadata>;

    private:
        [[nodiscard]] auto session_path(absl::string_view session_id) const
            -> std::filesystem::path;

        std::filesystem::path root_dir_;
    };

}  // namespace codeharness::services

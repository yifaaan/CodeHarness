#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "codeharness/config/paths.h"
#include "codeharness/engine/message.h"
#include "codeharness/services/session_storage.h"

using namespace codeharness;

namespace {

    class TempDirectory {
    public:
        explicit TempDirectory(std::string name)
            : path_{std::filesystem::temp_directory_path() / std::move(name)} {
            std::filesystem::remove_all(path_);
            std::filesystem::create_directories(path_);
        }

        ~TempDirectory() { std::filesystem::remove_all(path_); }

        TempDirectory(const TempDirectory&) = delete;
        auto operator=(const TempDirectory&) -> TempDirectory& = delete;

        TempDirectory(TempDirectory&&) = delete;
        auto operator=(TempDirectory&&) -> TempDirectory& = delete;

        [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

    private:
        std::filesystem::path path_;
    };

}  // namespace

TEST_CASE("session storage creates saves loads and lists sessions") {
    auto temp = TempDirectory{"codeharness-session-storage-test"};
    auto storage = services::SessionStorage{temp.path()};

    const auto metadata =
        storage.create_session("demo", "mock-model", std::filesystem::current_path());
    REQUIRE(metadata.ok());

    CHECK_FALSE(metadata->id.empty());
    CHECK(metadata->name == "demo");
    CHECK(metadata->model == "mock-model");

    const auto messages = std::vector<engine::ConversationMessage>{
        engine::ConversationMessage::from_user_text("hello"),
        engine::ConversationMessage{
            .role = engine::MessageRole::assistant,
            .content =
                {
                    engine::TextBlock{.text = "I will call a tool."},
                    engine::ToolUseBlock{
                        .id = "toolu_1",
                        .name = "read_file",
                        .input = nlohmann::json{{"path", "hello.txt"}},
                    },
                },
        },
        engine::ConversationMessage{
            .role = engine::MessageRole::user,
            .content =
                {
                    engine::ToolResultBlock{
                        .tool_use_id = "toolu_1",
                        .content = "alpha\nbeta\n",
                        .is_error = false,
                    },
                },
        },
    };

    CHECK(storage.save_messages(metadata->id, messages).ok());

    const auto loaded = storage.load_session(metadata->id);
    REQUIRE(loaded.ok());
    CHECK(loaded->metadata.id == metadata->id);
    CHECK(loaded->metadata.name == "demo");
    CHECK(loaded->metadata.model == "mock-model");
    REQUIRE(loaded->messages.size() == 3);
    CHECK(loaded->messages[0].text() == "hello");
    CHECK(loaded->messages[1].text() == "I will call a tool.");

    const auto tool_uses = loaded->messages[1].tool_uses();
    REQUIRE(tool_uses.size() == 1);
    CHECK(tool_uses[0].id == "toolu_1");
    CHECK(tool_uses[0].name == "read_file");
    CHECK(tool_uses[0].input.at("path").get<std::string>() == "hello.txt");

    const auto* tool_result =
        std::get_if<engine::ToolResultBlock>(&loaded->messages[2].content[0]);
    REQUIRE(tool_result != nullptr);
    CHECK(tool_result->tool_use_id == "toolu_1");
    CHECK(tool_result->content == "alpha\nbeta\n");
    CHECK_FALSE(tool_result->is_error);

    const auto sessions = storage.list_sessions();
    REQUIRE(sessions.ok());
    REQUIRE(sessions->size() == 1);
    CHECK(sessions->at(0).id == metadata->id);
}

TEST_CASE("session json round trips through nlohmann conversions") {
    const auto session = services::Session{
        .metadata =
            services::SessionMetadata{
                .id = "session_1",
                .name = "demo",
                .model = "mock-model",
                .cwd = std::filesystem::current_path(),
                .created_at = std::chrono::system_clock::now(),
                .updated_at = std::chrono::system_clock::now(),
            },
        .messages =
            {
                engine::ConversationMessage::from_user_text("hello"),
            },
    };

    const auto serialized = nlohmann::json(session);
    const auto parsed = services::session_from_json(serialized);
    REQUIRE(parsed.ok());

    CHECK(parsed->metadata.id == session.metadata.id);
    CHECK(parsed->metadata.name == session.metadata.name);
    CHECK(parsed->metadata.model == session.metadata.model);
    CHECK(parsed->metadata.cwd == session.metadata.cwd);
    REQUIRE(parsed->messages.size() == 1);
    CHECK(parsed->messages[0].text() == "hello");
}

TEST_CASE("session storage lists newest updated sessions first") {
    auto temp = TempDirectory{"codeharness-session-storage-order-test"};
    auto storage = services::SessionStorage{temp.path()};

    const auto older =
        storage.create_session("older", "mock-model", std::filesystem::current_path());
    REQUIRE(older.ok());
    std::this_thread::sleep_for(std::chrono::seconds{1});
    const auto newer =
        storage.create_session("newer", "mock-model", std::filesystem::current_path());
    REQUIRE(newer.ok());

    const auto sessions = storage.list_sessions();
    REQUIRE(sessions.ok());
    REQUIRE(sessions->size() == 2);
    CHECK(sessions->at(0).id == newer->id);
    CHECK(sessions->at(1).id == older->id);
}

TEST_CASE("SessionStorage::for_cwd uses same root for identical cwd") {
    auto data_root = TempDirectory{"codeharness-for-cwd-data-root"};
    auto project = TempDirectory{"codeharness-for-cwd-project"};

#if defined(_WIN32)
    REQUIRE(_putenv_s("CODEHARNESS_DATA_DIR", data_root.path().string().c_str()) == 0);
#else
    REQUIRE(setenv("CODEHARNESS_DATA_DIR", data_root.path().c_str(), 1) == 0);
#endif

    auto first = services::SessionStorage::for_cwd(project.path());
    auto second = services::SessionStorage::for_cwd(project.path());
    const auto metadata =
        first.create_session("demo", "mock-model", std::filesystem::path{project.path()});
    REQUIRE(metadata.ok());
    const auto loaded = second.load_session(metadata->id);
    REQUIRE(loaded.ok());
    CHECK(loaded->metadata.id == metadata->id);

#if defined(_WIN32)
    static_cast<void>(_putenv("CODEHARNESS_DATA_DIR="));
#else
    static_cast<void>(unsetenv("CODEHARNESS_DATA_DIR"));
#endif
}

TEST_CASE("project_sessions_directory is stable for the same cwd") {
    auto temp = TempDirectory{"codeharness-project-sessions-dir"};
    const auto a = config::paths::project_sessions_directory(temp.path(), false);
    const auto b = config::paths::project_sessions_directory(temp.path(), false);
    CHECK(a == b);
    CHECK(a.filename().string().starts_with(temp.path().filename().string()));
}

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "codeharness/tools/cron_create_tool.h"
#include "codeharness/tools/cron_delete_tool.h"
#include "codeharness/tools/cron_list_tool.h"
#include "codeharness/tools/remote_trigger_tool.h"

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

        [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] auto get_env(const std::string& name) -> std::optional<std::string> {
#if defined(_WIN32)
        char* raw = nullptr;
        std::size_t size = 0;
        if (::_dupenv_s(&raw, &size, name.c_str()) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        const auto owned = std::unique_ptr<char, decltype(&std::free)>{raw, &std::free};
        return std::string{owned.get()};
#else
        const auto* raw = std::getenv(name.c_str());
        if (raw == nullptr) {
            return std::nullopt;
        }
        return std::string{raw};
#endif
    }

    auto set_env(const std::string& name, const std::string& value) -> void {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name.c_str(), value.c_str()));
#else
        static_cast<void>(::setenv(name.c_str(), value.c_str(), 1));
#endif
    }

    auto unset_env(const std::string& name) -> void {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name.c_str(), ""));
#else
        static_cast<void>(::unsetenv(name.c_str()));
#endif
    }

    class ScopedEnvVar {
    public:
        ScopedEnvVar(std::string name, std::string value)
            : name_{std::move(name)}, old_value_{get_env(name_)} {
            set_env(name_, value);
        }

        ~ScopedEnvVar() {
            if (old_value_.has_value()) {
                set_env(name_, *old_value_);
            } else {
                unset_env(name_);
            }
        }

        ScopedEnvVar(const ScopedEnvVar&) = delete;
        auto operator=(const ScopedEnvVar&) -> ScopedEnvVar& = delete;

    private:
        std::string name_;
        std::optional<std::string> old_value_;
    };

}  // namespace

TEST_CASE("cron tools create list update and delete jobs") {
    auto temp = TempDirectory{"codeharness-cron-tools-test"};
    auto data_dir = ScopedEnvVar{"CODEHARNESS_DATA_DIR", temp.path().string()};
    const auto context = tools::ToolExecutionContext{.cwd = temp.path()};

    auto create_tool = tools::CronCreateTool{};
    auto list_tool = tools::CronListTool{};
    auto delete_tool = tools::CronDeleteTool{};

    const auto empty_list = list_tool.execute(nlohmann::json::object(), context);
    REQUIRE(empty_list.ok());
    CHECK(*empty_list == "No cron jobs configured.");
    CHECK(list_tool.is_read_only(nlohmann::json::object()));
    CHECK_FALSE(create_tool.is_read_only(nlohmann::json::object()));
    CHECK_FALSE(delete_tool.is_read_only(nlohmann::json::object()));

    const auto created = create_tool.execute(
        nlohmann::json{
            {"name", "daily-build"},
            {"schedule", "daily"},
            {"command", "xmake test"},
        },
        context);
    REQUIRE(created.ok());
    CHECK(*created == "Created cron job daily-build");

    const auto updated = create_tool.execute(
        nlohmann::json{
            {"name", "daily-build"},
            {"schedule", "hourly"},
            {"command", "xmake run codeharness_tests"},
        },
        context);
    REQUIRE(updated.ok());

    const auto populated_list = list_tool.execute(nlohmann::json::object(), context);
    REQUIRE(populated_list.ok());
    CHECK(*populated_list == "daily-build [hourly] -> xmake run codeharness_tests");

    const auto deleted = delete_tool.execute(nlohmann::json{{"name", "daily-build"}}, context);
    REQUIRE(deleted.ok());
    CHECK(*deleted == "Deleted cron job daily-build");

    const auto final_list = list_tool.execute(nlohmann::json::object(), context);
    REQUIRE(final_list.ok());
    CHECK(*final_list == "No cron jobs configured.");
}

TEST_CASE("remote trigger tool runs a configured cron job") {
    auto temp = TempDirectory{"codeharness-remote-trigger-test"};
    auto data_dir = ScopedEnvVar{"CODEHARNESS_DATA_DIR", temp.path().string()};
    const auto context = tools::ToolExecutionContext{.cwd = temp.path()};

    auto create_tool = tools::CronCreateTool{};
    const auto created = create_tool.execute(
        nlohmann::json{
            {"name", "say-hello"},
            {"schedule", "manual"},
            {"command", "echo triggered"},
        },
        context);
    REQUIRE(created.ok());

    auto trigger_tool = tools::RemoteTriggerTool{};
    const auto triggered = trigger_tool.execute(
        nlohmann::json{{"name", "say-hello"}, {"timeout_seconds", 5}}, context);
    REQUIRE(triggered.ok());
    CHECK(*triggered == "Triggered say-hello\ntriggered");
    CHECK_FALSE(trigger_tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("cron tools validate names and missing deletes") {
    auto temp = TempDirectory{"codeharness-cron-tools-validation-test"};
    auto data_dir = ScopedEnvVar{"CODEHARNESS_DATA_DIR", temp.path().string()};
    const auto context = tools::ToolExecutionContext{.cwd = temp.path()};

    auto create_tool = tools::CronCreateTool{};
    const auto empty_name = create_tool.execute(
        nlohmann::json{{"name", ""}, {"schedule", "daily"}, {"command", "xmake test"}},
        context);
    REQUIRE_FALSE(empty_name.ok());
    CHECK(empty_name.status().message() == "name must not be empty");

    auto delete_tool = tools::CronDeleteTool{};
    const auto missing = delete_tool.execute(nlohmann::json{{"name", "missing"}}, context);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.status().message() == "Cron job not found: missing");

    auto trigger_tool = tools::RemoteTriggerTool{};
    const auto missing_trigger = trigger_tool.execute(nlohmann::json{{"name", "missing"}}, context);
    REQUIRE_FALSE(missing_trigger.ok());
    CHECK(missing_trigger.status().message() == "Cron job not found: missing");
}

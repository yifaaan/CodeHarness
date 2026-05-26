#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "codeharness/tools/config_tool.h"

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

    [[nodiscard]] auto env_string(const char* name) -> std::optional<std::string> {
#if defined(_WIN32)
        char* raw = nullptr;
        std::size_t size = 0;
        if (::_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        const auto owned = std::unique_ptr<char, decltype(&std::free)>{raw, &std::free};
        return std::string{owned.get()};
#else
        const auto* raw = std::getenv(name);
        if (raw == nullptr) {
            return std::nullopt;
        }
        return std::string{raw};
#endif
    }

    class ScopedEnvVar {
    public:
        ScopedEnvVar(std::string name, std::string value)
            : name_{std::move(name)}, previous_{env_string(name_.c_str())} {
#if defined(_WIN32)
            REQUIRE(::_putenv_s(name_.c_str(), value.c_str()) == 0);
#else
            REQUIRE(::setenv(name_.c_str(), value.c_str(), 1) == 0);
#endif
        }

        ~ScopedEnvVar() {
#if defined(_WIN32)
            if (previous_.has_value()) {
                static_cast<void>(::_putenv_s(name_.c_str(), previous_->c_str()));
            } else {
                static_cast<void>(::_putenv_s(name_.c_str(), ""));
            }
#else
            if (previous_.has_value()) {
                static_cast<void>(::setenv(name_.c_str(), previous_->c_str(), 1));
            } else {
                static_cast<void>(::unsetenv(name_.c_str()));
            }
#endif
        }

        ScopedEnvVar(const ScopedEnvVar&) = delete;
        auto operator=(const ScopedEnvVar&) -> ScopedEnvVar& = delete;

        ScopedEnvVar(ScopedEnvVar&&) = delete;
        auto operator=(ScopedEnvVar&&) -> ScopedEnvVar& = delete;

    private:
        std::string name_;
        std::optional<std::string> previous_;
    };

    [[nodiscard]] auto read_json(const std::filesystem::path& path) -> nlohmann::json {
        std::ifstream file{path};
        return nlohmann::json::parse(file);
    }

}  // namespace

TEST_CASE("config tool shows resolved settings") {
    auto temp = TempDirectory{"codeharness-config-tool-show-test"};
    auto env = ScopedEnvVar{"CODEHARNESS_CONFIG_DIR", temp.path().string()};

    auto tool = tools::ConfigTool{};
    const auto result = tool.execute(
        nlohmann::json{{"action", "show"}},
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    const auto shown = nlohmann::json::parse(*result);
    CHECK(shown.at("api").at("model").is_string());
    CHECK(shown.at("permissions").at("mode").get<std::string>() == "default");
    CHECK(tool.is_read_only(nlohmann::json{{"action", "show"}}));
}

TEST_CASE("config tool sets supported settings") {
    auto temp = TempDirectory{"codeharness-config-tool-set-test"};
    auto env = ScopedEnvVar{"CODEHARNESS_CONFIG_DIR", temp.path().string()};

    auto tool = tools::ConfigTool{};
    const auto model_result = tool.execute(
        nlohmann::json{
            {"action", "set"},
            {"key", "model"},
            {"value", "test-model"},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(model_result.ok());
    CHECK(*model_result == "Updated model");
    CHECK_FALSE(tool.is_read_only(nlohmann::json{{"action", "set"}}));

    const auto tokens_result = tool.execute(
        nlohmann::json{
            {"action", "set"},
            {"key", "max_tokens"},
            {"value", "1234"},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(tokens_result.ok());

    const auto saved = read_json(temp.path() / "settings.json");
    CHECK(saved.at("api").at("model").get<std::string>() == "test-model");
    CHECK(saved.at("api").at("max_tokens").get<int>() == 1234);
}

TEST_CASE("config tool rejects unsupported or malformed settings") {
    auto temp = TempDirectory{"codeharness-config-tool-invalid-test"};
    auto env = ScopedEnvVar{"CODEHARNESS_CONFIG_DIR", temp.path().string()};

    auto tool = tools::ConfigTool{};
    const auto unknown = tool.execute(
        nlohmann::json{
            {"action", "set"},
            {"key", "theme"},
            {"value", "dark"},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.status().message() == "Unknown config key: theme");

    const auto invalid_number = tool.execute(
        nlohmann::json{
            {"action", "set"},
            {"key", "max_tokens"},
            {"value", "nope"},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE_FALSE(invalid_number.ok());
    CHECK(invalid_number.status().message() == "max_tokens must be a positive integer");
}

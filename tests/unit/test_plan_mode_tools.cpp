#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>

#include "codeharness/tools/enter_plan_mode_tool.h"
#include "codeharness/tools/exit_plan_mode_tool.h"

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

    private:
        std::string name_;
        std::optional<std::string> previous_;
    };

    [[nodiscard]] auto read_settings(const std::filesystem::path& root) -> nlohmann::json {
        std::ifstream file{root / "settings.json"};
        return nlohmann::json::parse(file);
    }

}  // namespace

TEST_CASE("plan mode tools switch permission mode in settings") {
    auto temp = TempDirectory{"codeharness-plan-mode-tools-test"};
    auto env = ScopedEnvVar{"CODEHARNESS_CONFIG_DIR", temp.path().string()};
    const auto context = tools::ToolExecutionContext{.cwd = std::filesystem::current_path()};

    auto enter_tool = tools::EnterPlanModeTool{};
    const auto enter_result = enter_tool.execute(nlohmann::json::object(), context);
    REQUIRE(enter_result.ok());
    CHECK(*enter_result == "Permission mode set to plan");
    CHECK(read_settings(temp.path()).at("permissions").at("mode") == "plan");
    CHECK_FALSE(enter_tool.is_read_only(nlohmann::json::object()));

    auto exit_tool = tools::ExitPlanModeTool{};
    const auto exit_result = exit_tool.execute(nlohmann::json::object(), context);
    REQUIRE(exit_result.ok());
    CHECK(*exit_result == "Permission mode set to default");
    CHECK(read_settings(temp.path()).at("permissions").at("mode") == "default");
    CHECK_FALSE(exit_tool.is_read_only(nlohmann::json::object()));
}

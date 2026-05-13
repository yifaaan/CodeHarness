#pragma once

#include <absl/status/statusor.h>

#include <filesystem>
#include <string>

#include "codeharness/api/openai_client.h"
#include "codeharness/config/setting.h"
#include "codeharness/engine/query_engine.h"
#include "codeharness/permissions/checker.h"
#include "codeharness/tools/base.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness::app {

    class RuntimeBundle {
    public:
        static auto create(config::Settings settings,
                           std::filesystem::path cwd,
                           std::string system_prompt = default_system_prompt())
            -> absl::StatusOr<RuntimeBundle>;

        RuntimeBundle(const RuntimeBundle&) = delete;
        auto operator=(const RuntimeBundle&) -> RuntimeBundle& = delete;

        RuntimeBundle(RuntimeBundle&& other) noexcept;
        auto operator=(RuntimeBundle&& other) noexcept -> RuntimeBundle& = delete;

        ~RuntimeBundle() = default;

        [[nodiscard]] static auto default_system_prompt() -> std::string;

        [[nodiscard]] auto engine() noexcept -> engine::QueryEngine& { return engine_; }
        [[nodiscard]] auto engine() const noexcept -> const engine::QueryEngine& { return engine_; }

        [[nodiscard]] auto settings() const noexcept -> const config::Settings& {
            return settings_;
        }

        [[nodiscard]] auto cwd() const noexcept -> const std::filesystem::path& { return cwd_; }

        [[nodiscard]] auto tools() noexcept -> tools::ToolRegistry& { return tools_; }
        [[nodiscard]] auto tools() const noexcept -> const tools::ToolRegistry& { return tools_; }

        [[nodiscard]] auto permissions() const noexcept -> const permissions::PermissionChecker& {
            return permissions_;
        }

        auto set_model(std::string model) -> void;
        auto set_system_prompt(std::string system_prompt) -> void;
        auto clear_conversation() noexcept -> void;

    private:
        RuntimeBundle(config::Settings settings,
                      std::filesystem::path cwd,
                      std::string system_prompt,
                      api::OpenAIClient api,
                      tools::ToolRegistry tools,
                      permissions::PermissionChecker permissions);

        config::Settings settings_;
        std::filesystem::path cwd_;
        std::string system_prompt_;
        api::OpenAIClient api_;
        tools::ToolRegistry tools_;
        permissions::PermissionChecker permissions_;
        engine::QueryEngine engine_;
    };

}  // namespace codeharness::app

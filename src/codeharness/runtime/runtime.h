#pragma once

#include "codeharness/commands/command_registry.h"
#include "codeharness/coordinator/runtime.h"
#include "codeharness/core/result.h"
#include "codeharness/engine/engine.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/plugins/plugin_loader.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/skills/skill_registry.h"
#include "codeharness/tools/tool_registry.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace codeharness::runtime
{

struct RuntimeBundleOptions
{
    std::filesystem::path cwd;
    std::filesystem::path memory_root;
    PermissionMode permission_mode = PermissionMode::Default;
    bool load_default_user_plugins = true;
};

class RuntimeBundle
{
public:
    RuntimeBundle(std::filesystem::path cwd,
                  PermissionMode permission_mode,
                  SkillRegistryLoadResult loaded_skills,
                  memory::MemoryStore memory_store,
                  std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime);

    RuntimeBundle(const RuntimeBundle&) = delete;
    auto operator=(const RuntimeBundle&) -> RuntimeBundle& = delete;
    RuntimeBundle(RuntimeBundle&&) = delete;
    auto operator=(RuntimeBundle&&) -> RuntimeBundle& = delete;

    [[nodiscard]] auto cwd() const noexcept -> const std::filesystem::path&;
    [[nodiscard]] auto permission_mode() const noexcept -> PermissionMode;

    [[nodiscard]] auto skills() const noexcept -> const SkillRegistry&;
    [[nodiscard]] auto plugins() const noexcept -> const std::vector<LoadedPlugin>&;
    [[nodiscard]] auto memory_store() noexcept -> memory::MemoryStore&;
    [[nodiscard]] auto memory_store() const noexcept -> const memory::MemoryStore&;
    [[nodiscard]] auto commands() const noexcept -> const CommandRegistry&;
    [[nodiscard]] auto tools() const noexcept -> const ToolRegistry&;
    [[nodiscard]] auto coordinator_runtime() noexcept -> coordinator::CoordinatorRuntime&;
    [[nodiscard]] auto coordinator_runtime() const noexcept -> const coordinator::CoordinatorRuntime&;

    auto build_run_request(std::string_view prompt, int max_turns) -> Result<RunRequest>;
    auto run_prompt(std::string_view prompt, int max_turns, const EngineEventSink& sink) -> Result<RunResult>;

private:
    std::filesystem::path cwd_;
    PermissionMode permission_mode_ = PermissionMode::Default;
    SkillRegistryLoadResult loaded_skills_;
    memory::MemoryStore memory_store_;
    CommandRegistry commands_;
    std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime_;
    ToolRegistry tools_;
    PermissionChecker permissions_;
    EchoProvider provider_;
    Engine engine_;
};

auto create_runtime_bundle(RuntimeBundleOptions options) -> Result<std::unique_ptr<RuntimeBundle>>;

} // namespace codeharness::runtime

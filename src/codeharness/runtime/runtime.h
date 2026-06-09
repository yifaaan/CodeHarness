//==============================================================================
// runtime.h — RuntimeBundle（运行时包）
//
// 架构角色：系统组装层
// 职责：将 engine、tool、skill、coordinator、memory、permission 等全部
//       子系统组装成一个可用的运行时包。这是 monte（main）函数直接使用的
//       最高层抽象。
//
// RuntimeBundle 拥有的子系统：
//   SkillRegistry       — 技能注册表（slash commands 和 agent skills）
//   MemoryStore         — 记忆存储（持久化的用户/项目记忆）
//   CommandRegistry     — 命令注册表（/help, /skills, /model 等）
//   ToolRegistry        — 工具注册表（LLM 可调用的 bash/read/edit 等）
//   CoordinatorRuntime  — coordinator（子 agent 管理）
//   PermissionChecker   — 权限检查器
//   Engine              — LLM 交互引擎
//
// 设计原理：
//   这是典型的"组合根"（Composition Root）模式——在应用启动时一次性
//   创建所有依赖项，而不是让每个组件自行发现依赖。create_runtime_bundle
//   就是组装工厂：
//     1. 加载 skills（技能 + 插件）
//     2. 创建 memory store
//     3. 创建 coordinator runtime
//     4. 构造 ToolRegistry（工具 + coordinator 工具 + mailbox 工具）
//     5. 构造 CommandRegistry（内置命令）
//     6. 构造 PermissionChecker
//     7. 构造 Engine（LLM 调用循环）
//
//   两个主要操作：
//     build_run_request  — 构造 RunRequest（系统提示词 + 用户提示词）
//     run_prompt         — 执行一次完整的 prompt → LLM → tool_use → 结果
//
//   运行时包被设计为不可复制、不可移动。其生命周期从创建到进程结束。
//==============================================================================

#pragma once

#include "codeharness/commands/command_registry.h"
#include "codeharness/coordinator/runtime.h"
#include "codeharness/core/result.h"
#include "codeharness/engine/engine.h"
#include "codeharness/hooks/hook_executor.h"
#include "codeharness/hooks/hook_registry.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/plugins/plugin_loader.h"
#include "codeharness/provider/provider_config.h"
#include "codeharness/sessions/session_store.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/skills/skill_registry.h"
#include "codeharness/tools/tool_registry.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace codeharness::runtime
{

struct RuntimeModelProfile
{
    std::string id;
    std::string label;
    std::string description;
    ProviderConfig provider_config;
};

struct RuntimeBundleOptions
{
    std::filesystem::path cwd;
    std::filesystem::path memory_root;
    PermissionSettings permission;
    std::vector<HookDefinition> hooks;
    bool load_default_user_plugins = true;
    ProviderConfig provider_config;
    std::vector<RuntimeModelProfile> model_profiles;
    std::string active_model_profile_id;
};

struct RunPromptOptions
{
    int max_turns = 10;
    PermissionPromptHandler permission_prompt;
    UserQuestionHandler user_question;
    CancellationToken cancellation;
};

class RuntimeBundle
{
public:
    RuntimeBundle(std::filesystem::path cwd,
                  PermissionSettings permission,
                  HookRegistry hooks,
                  SkillRegistryLoadResult loaded_skills,
                  memory::MemoryStore memory_store,
                  std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime,
                  ToolRegistry tools,
                  std::unique_ptr<Provider> provider,
                  std::string model,
                  sessions::SessionStore sessions,
                  std::string provider_type = "echo",
                  std::string base_url = {},
                  std::string profile_id = "default",
                  std::string profile_label = "Default",
                  std::vector<RuntimeModelProfile> model_profiles = {});

    RuntimeBundle(const RuntimeBundle&) = delete;
    auto operator=(const RuntimeBundle&) -> RuntimeBundle& = delete;
    RuntimeBundle(RuntimeBundle&&) = delete;
    auto operator=(RuntimeBundle&&) -> RuntimeBundle& = delete;

    [[nodiscard]] auto cwd() const noexcept -> const std::filesystem::path&;
    [[nodiscard]] auto permission_mode() const noexcept -> PermissionMode;
    auto set_permission_mode(PermissionMode mode) -> void;
    [[nodiscard]] auto current_model_profile() const -> RuntimeModelProfile;
    [[nodiscard]] auto model_profiles() const noexcept -> const std::vector<RuntimeModelProfile>&;
    [[nodiscard]] auto find_model_profile(std::string_view id_or_model) const -> std::optional<RuntimeModelProfile>;
    auto switch_model_profile(const RuntimeModelProfile& profile) -> Result<RuntimeModelProfile>;

    [[nodiscard]] auto skills() const noexcept -> const SkillRegistry&;
    [[nodiscard]] auto plugins() const noexcept -> const std::vector<LoadedPlugin>&;
    [[nodiscard]] auto memory_store() noexcept -> memory::MemoryStore&;
    [[nodiscard]] auto memory_store() const noexcept -> const memory::MemoryStore&;
    [[nodiscard]] auto commands() const noexcept -> const CommandRegistry&;
    [[nodiscard]] auto tools() const noexcept -> const ToolRegistry&;
    [[nodiscard]] auto coordinator_runtime() noexcept -> coordinator::CoordinatorRuntime&;
    [[nodiscard]] auto coordinator_runtime() const noexcept -> const coordinator::CoordinatorRuntime&;

    [[nodiscard]] auto sessions() noexcept -> sessions::SessionStore&;
    [[nodiscard]] auto sessions() const noexcept -> const sessions::SessionStore&;
    [[nodiscard]] auto active_session_summary() const noexcept -> std::optional<SessionCommandSummary>;
    [[nodiscard]] auto latest_usage() const noexcept -> sessions::UsageSnapshot;
    auto resume_session(std::string_view id) -> Result<SessionCommandSummary>;

    auto build_run_request(std::string_view prompt, int max_turns) -> Result<RunRequest>;
    auto run_prompt(std::string_view prompt, int max_turns, const EngineEventSink& sink) -> Result<RunResult>;
    auto run_prompt(std::string_view prompt, const RunPromptOptions& options, const EngineEventSink& sink)
        -> Result<RunResult>;

private:
    auto remember_permission_for_session(const PermissionPrompt& prompt) -> void;

    std::filesystem::path cwd_;
    std::string profile_id_;
    std::string profile_label_;
    std::string provider_type_;
    std::string model_;
    std::string base_url_;
    std::vector<RuntimeModelProfile> model_profiles_;
    PermissionMode permission_mode_ = PermissionMode::Default;
    SkillRegistryLoadResult loaded_skills_;
    memory::MemoryStore memory_store_;
    sessions::SessionStore sessions_;
    CommandRegistry commands_;
    std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime_;
    ToolRegistry tools_;
    PermissionChecker permissions_;
    HookRegistry hooks_;
    HookExecutor hook_executor_;
    std::unique_ptr<Provider> provider_;
    Engine engine_;
    std::optional<sessions::SessionSnapshot> active_session_;
};

auto create_runtime_bundle(RuntimeBundleOptions options) -> Result<std::unique_ptr<RuntimeBundle>>;

} // namespace codeharness::runtime

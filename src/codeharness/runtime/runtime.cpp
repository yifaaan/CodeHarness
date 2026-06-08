//==============================================================================
// runtime.cpp — RuntimeBundle 实现
//
// 这部分的核心逻辑在 RuntimeBundle 的构造函数和 create_runtime_bundle
// 工厂函数中。构造时的关键细节：
//
//   1. create_command_registry — 构建内置命令（slash commands）
//      这些命令包括 /help, /skills, /model, /clear 等。
//      SkillRegistry 中的每个 skill 也可能注册自己的命令。
//
//   2. create_tool_registry — 构建 LLM 可调用工具
//      这是 LLM agent 的核心能力来源：
//      - 文件操作：ReadFileTool, EditFileTool, GlobTool, GrepTool
//      - 命令执行：BashTool
//      - 技能调用：SkillTool
//      - 任务管理：task_create, task_list, task_get, task_output, task_stop
//      - 子 agent：agent tool（通过 register_task_tools 注入 spawn_handler）
//      - 消息：mailbox tools（register_mailbox_tools）
//
//   3. load_relevant_memories_for_prompt — 记忆检索
//      在构建 prompt 时，从 memory store 中检索与当前提示词相关的记忆。
//      这些记忆以 <relevant-memories> XML 标签嵌入系统提示词。
//
//   4. build_run_request — 构造完整请求
//      合并系统提示词 + 项目上下文文件 + 相关记忆 + 可用 skill 列表
//      + 可用命令列表 → 形成完整的 RunRequest，交给 Engine 执行。
//==============================================================================

#include "codeharness/runtime/runtime.h"

#include "codeharness/mailbox/mailbox_tools.h"
#include "codeharness/provider/anthropic_provider.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/provider/openai_provider.h"
#include "codeharness/prompts/project_context.h"
#include "codeharness/prompts/system_prompt.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/tasks/task_tools.h"
#include "codeharness/tools/bash_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/skill_tool.h"

#include "codeharness/core/strings.h"

#include <nonstd/expected.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace codeharness::runtime
{

namespace
{

auto load_relevant_memories_for_prompt(const memory::MemoryStore& store,
                                       std::string_view prompt,
                                       std::size_t max_results = 5)
    -> Result<std::vector<RelevantMemory>>
{
    auto entries = store.search(prompt, max_results);
    if (!entries)
    {
        return nonstd::make_unexpected(entries.error());
    }

    std::vector<RelevantMemory> memories;
    memories.reserve(entries->size());
    for (const auto& entry : *entries)
    {
        memories.push_back(RelevantMemory{
            .title = entry.header.title,
            .content = entry.body,
        });
    }

    return memories;
}

auto create_command_registry(SkillRegistry& skills,
                             memory::MemoryStore& memory_store,
                             sessions::SessionStore& session_store,
                             std::function<Result<SessionCommandSummary>(std::string_view id)> resume_session,
                             std::span<const LoadedPlugin> plugins) -> CommandRegistry
{
    return build_builtin_command_registry(
        skills,
        BuiltinCommandRegistryOptions{
            .memory_store = &memory_store,
            .session_store = &session_store,
            .resume_session = std::move(resume_session),
            .plugins = plugins,
        });
}

auto create_tool_registry(const SkillRegistry& skills, coordinator::CoordinatorRuntime& coordinator_runtime) -> ToolRegistry
{
    ToolRegistry tools;
    tools.add(std::make_unique<ReadFileTool>());
    tools.add(std::make_unique<EditFileTool>());
    tools.add(std::make_unique<GlobTool>());
    tools.add(std::make_unique<GrepTool>());
    tools.add(std::make_unique<BashTool>());
    tools.add(std::make_unique<SkillTool>(skills));
    tasks::register_task_tools(tools, coordinator_runtime.task_manager(), coordinator_runtime.spawn_handler());
    mailbox::register_mailbox_tools(tools, coordinator_runtime.mailbox(), &coordinator_runtime.task_manager());
    return tools;
}

auto session_summary_from(const sessions::SessionSnapshot& snapshot) -> SessionCommandSummary
{
    return SessionCommandSummary{
        .session_id = snapshot.session_id,
        .model = snapshot.model,
        .summary = snapshot.summary,
        .message_count = snapshot.message_count,
    };
}

auto extract_system_prompt(const std::vector<Message>& messages) -> std::string
{
    for (const auto& msg : messages)
    {
        if (msg.role == Role::System)
        {
            return collect_text(msg);
        }
    }

    return {};
}

auto extract_summary(const std::vector<Message>& messages, std::size_t max_chars = 80) -> std::string
{
    for (const auto& msg : messages)
    {
        if (msg.role != Role::User)
        {
            continue;
        }

        auto text = collect_text(msg);
        if (!text.empty())
        {
            if (text.size() > max_chars)
            {
                text.resize(max_chars);
            }
            return text;
        }
    }

    return {};
}

auto unix_timestamp_now() -> double
{
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class ScopedCurrentPath
{
public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
    {
        std::error_code error;
        previous_ = std::filesystem::current_path(error);
        if (error)
        {
            failed_ = make_error(ErrorKind::Io, "failed to read current directory: " + error.message());
            return;
        }

        std::filesystem::current_path(path, error);
        if (error)
        {
            failed_ = make_error(ErrorKind::Io, "failed to change cwd: " + error.message());
        }
    }

    ~ScopedCurrentPath()
    {
        if (!previous_.empty())
        {
            std::error_code ignored;
            std::filesystem::current_path(previous_, ignored);
        }
    }

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    auto operator=(const ScopedCurrentPath&) -> ScopedCurrentPath& = delete;

    [[nodiscard]] auto error() const -> const std::optional<CodeHarnessError>&
    {
        return failed_;
    }

private:
    std::filesystem::path previous_;
    std::optional<CodeHarnessError> failed_;
};

} // namespace

RuntimeBundle::RuntimeBundle(std::filesystem::path cwd,
                             PermissionSettings permission,
                             SkillRegistryLoadResult loaded_skills,
                             memory::MemoryStore memory_store,
                             std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime,
                             ToolRegistry tools,
                             std::unique_ptr<Provider> provider,
                             std::string model,
                             sessions::SessionStore sessions) :
    cwd_(std::move(cwd)),
    model_(std::move(model)),
    permission_mode_(permission.mode),
    loaded_skills_(std::move(loaded_skills)),
    memory_store_(std::move(memory_store)),
    sessions_(std::move(sessions)),
    commands_(create_command_registry(
        loaded_skills_.registry,
        memory_store_,
        sessions_,
        [this](std::string_view id) { return resume_session(id); },
        loaded_skills_.plugins)),
    coordinator_runtime_(std::move(coordinator_runtime)),
    tools_(std::move(tools)),
    permissions_(std::move(permission)),
    provider_(std::move(provider)),
    engine_(*provider_, tools_, &permissions_)
{
}

auto RuntimeBundle::cwd() const noexcept -> const std::filesystem::path&
{
    return cwd_;
}

auto RuntimeBundle::permission_mode() const noexcept -> PermissionMode
{
    return permission_mode_;
}

auto RuntimeBundle::set_permission_mode(PermissionMode mode) -> void
{
    permission_mode_ = mode;
    auto settings = permissions_.settings();
    settings.mode = mode;
    permissions_ = PermissionChecker(std::move(settings));
    engine_.set_permission_checker(&permissions_);
}

auto RuntimeBundle::skills() const noexcept -> const SkillRegistry&
{
    return loaded_skills_.registry;
}

auto RuntimeBundle::plugins() const noexcept -> const std::vector<LoadedPlugin>&
{
    return loaded_skills_.plugins;
}

auto RuntimeBundle::memory_store() noexcept -> memory::MemoryStore&
{
    return memory_store_;
}

auto RuntimeBundle::memory_store() const noexcept -> const memory::MemoryStore&
{
    return memory_store_;
}

auto RuntimeBundle::commands() const noexcept -> const CommandRegistry&
{
    return commands_;
}

auto RuntimeBundle::tools() const noexcept -> const ToolRegistry&
{
    return tools_;
}

auto RuntimeBundle::coordinator_runtime() noexcept -> coordinator::CoordinatorRuntime&
{
    return *coordinator_runtime_;
}

auto RuntimeBundle::coordinator_runtime() const noexcept -> const coordinator::CoordinatorRuntime&
{
    return *coordinator_runtime_;
}

auto RuntimeBundle::sessions() noexcept -> sessions::SessionStore&
{
    return sessions_;
}

auto RuntimeBundle::sessions() const noexcept -> const sessions::SessionStore&
{
    return sessions_;
}

auto RuntimeBundle::active_session_summary() const noexcept -> std::optional<SessionCommandSummary>
{
    if (!active_session_)
    {
        return std::nullopt;
    }

    return session_summary_from(*active_session_);
}

auto RuntimeBundle::resume_session(std::string_view id) -> Result<SessionCommandSummary>
{
    const auto session_id = std::string{id.empty() ? std::string_view{"latest"} : id};
    auto loaded = sessions_.load_by_id(session_id);
    if (!loaded)
    {
        return nonstd::make_unexpected(loaded.error());
    }

    if (!*loaded)
    {
        return fail<SessionCommandSummary>(ErrorKind::NotFound, "session not found: " + session_id);
    }

    active_session_ = std::move(**loaded);
    return session_summary_from(*active_session_);
}

auto RuntimeBundle::build_run_request(std::string_view prompt, int max_turns) -> Result<RunRequest>
{
    auto project_context_files = load_project_context_files(cwd_);
    if (!project_context_files)
    {
        return nonstd::make_unexpected(project_context_files.error());
    }

    PromptBuildRequest prompt_request;
    prompt_request.cwd = cwd_;
    prompt_request.latest_user_prompt = std::string{prompt};
    prompt_request.available_skills = loaded_skills_.registry.list();
    prompt_request.available_commands = commands_.list();
    prompt_request.project_context_files = std::move(*project_context_files);
    prompt_request.permission_mode = permission_mode_;

    auto relevant_memories = load_relevant_memories_for_prompt(memory_store_, prompt);
    if (!relevant_memories)
    {
        return nonstd::make_unexpected(relevant_memories.error());
    }
    prompt_request.relevant_memories = std::move(*relevant_memories);

    auto system_prompt = SystemPromptBuilder{}.build(prompt_request);
    if (!system_prompt)
    {
        return nonstd::make_unexpected(system_prompt.error());
    }

    return RunRequest{
        .prompt = std::string{prompt},
        .system_prompt = std::move(*system_prompt),
        .initial_messages = active_session_ ? std::make_optional(active_session_->messages) : std::nullopt,
        .options = EngineOptions{.max_turns = max_turns},
    };
}

auto RuntimeBundle::run_prompt(std::string_view prompt, int max_turns, const EngineEventSink& sink) -> Result<RunResult>
{
    return run_prompt(prompt, RunPromptOptions{.max_turns = max_turns}, sink);
}

auto RuntimeBundle::run_prompt(std::string_view prompt, const RunPromptOptions& options, const EngineEventSink& sink)
    -> Result<RunResult>
{
    // Handle plan mode toggle commands directly (no engine needed).
    auto trimmed = trim(prompt);
    if (trimmed == "/plan" || trimmed == "/plan on" || trimmed == "/plan enter")
    {
        set_permission_mode(PermissionMode::Plan);
        return RunResult{.output_text = "Entered plan mode. Read-only tools only."};
    }
    if (trimmed == "/act" || trimmed == "/plan off" || trimmed == "/plan exit")
    {
        set_permission_mode(PermissionMode::Default);
        return RunResult{.output_text = "Default mode. Mutating tools allowed with confirmation."};
    }
    if (trimmed == "/mode" || trimmed == "/permissions")
    {
        auto mode_str = [this]() -> std::string_view {
            switch (permission_mode_) {
                case PermissionMode::Plan: return "plan";
                case PermissionMode::FullAuto: return "full_auto";
                default: return "default";
            }
        }();
        return RunResult{.output_text = std::string{"Current permission mode: "} + std::string{mode_str}};
    }

    auto request = build_run_request(prompt, options.max_turns);
    if (!request)
    {
        return nonstd::make_unexpected(request.error());
    }
    request->permission_prompt = options.permission_prompt;
    request->cancellation = options.cancellation;

    ScopedCurrentPath current_path{cwd_};
    if (current_path.error())
    {
        return nonstd::make_unexpected(*current_path.error());
    }

    auto result = engine_.run_streaming(*request, sink);
    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    sessions::SessionSnapshot snapshot;
    if (active_session_)
    {
        snapshot = std::move(*active_session_);
    }
    else
    {
        snapshot.session_id = sessions::generate_session_id();
        snapshot.cwd = cwd_;
        snapshot.created_at = unix_timestamp_now();
    }

    snapshot.cwd = cwd_;
    snapshot.model = model_;
    snapshot.messages = result->messages;
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    snapshot.system_prompt = extract_system_prompt(snapshot.messages);
    if (snapshot.created_at == 0.0)
    {
        snapshot.created_at = unix_timestamp_now();
    }
    snapshot.summary = extract_summary(snapshot.messages);

    auto saved = sessions_.save(snapshot);
    if (!saved)
    {
        spdlog::warn("failed to save session: {}", saved.error().message);
    }

    active_session_ = std::move(snapshot);
    return std::move(*result);
}

auto create_memory_store(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> Result<memory::MemoryStore>
{
    if (memory_root.empty())
    {
        return memory::MemoryStore::for_project(cwd);
    }

    return memory::MemoryStore::for_project(cwd, memory_root);
}

auto tool_descriptions_from(const ToolRegistry& tools) -> std::vector<std::pair<std::string, std::string>>
{
    std::vector<std::pair<std::string, std::string>> descriptions;
    for (const auto& name : tools.names())
    {
        if (const auto* tool = tools.find(name))
        {
            descriptions.emplace_back(name, tool->description());
        }
    }
    return descriptions;
}

auto create_provider(const ProviderConfig& config, const ToolRegistry& tools) -> Result<std::unique_ptr<Provider>>
{
    if (config.type == "echo" || config.type.empty())
    {
        return std::make_unique<EchoProvider>();
    }

    if (config.type == "openai")
    {
        if (config.api_key.empty())
        {
            return fail<std::unique_ptr<Provider>>(ErrorKind::Config, "openai provider requires an API key");
        }
        return std::make_unique<OpenAIProvider>(config, tool_descriptions_from(tools));
    }

    if (config.type == "anthropic")
    {
        if (config.api_key.empty())
        {
            return fail<std::unique_ptr<Provider>>(ErrorKind::Config, "anthropic provider requires an API key");
        }
        return std::make_unique<AnthropicProvider>(config, tool_descriptions_from(tools));
    }

    return fail<std::unique_ptr<Provider>>(ErrorKind::InvalidArgument, "unknown provider type: " + config.type);
}

auto create_runtime_bundle(RuntimeBundleOptions options) -> Result<std::unique_ptr<RuntimeBundle>>
{
    if (options.cwd.empty())
    {
        options.cwd = std::filesystem::current_path();
    }

    SkillLoadOptions skill_options;
    skill_options.plugin_options.load_default_user_plugins = options.load_default_user_plugins;

    auto loaded_skills = load_skill_registry_with_plugins(options.cwd, std::move(skill_options));
    if (!loaded_skills)
    {
        return nonstd::make_unexpected(loaded_skills.error());
    }

    auto memory_store = create_memory_store(options.cwd, options.memory_root);
    if (!memory_store)
    {
        return nonstd::make_unexpected(memory_store.error());
    }

    auto coordinator_runtime = coordinator::create_default_runtime(options.cwd);
    if (!coordinator_runtime)
    {
        return nonstd::make_unexpected(coordinator_runtime.error());
    }

    auto tools = create_tool_registry(loaded_skills->registry, **coordinator_runtime);
    auto provider = create_provider(options.provider_config, tools);
    if (!provider)
    {
        return nonstd::make_unexpected(provider.error());
    }

    auto session_store = sessions::SessionStore::for_project(options.cwd);
    if (!session_store)
    {
        return nonstd::make_unexpected(session_store.error());
    }

    return std::make_unique<RuntimeBundle>(std::move(options.cwd),
                                           std::move(options.permission),
                                           std::move(*loaded_skills),
                                           std::move(*memory_store),
                                           std::move(*coordinator_runtime),
                                           std::move(tools),
                                           std::move(*provider),
                                           options.provider_config.model,
                                           std::move(*session_store));
}

} // namespace codeharness::runtime

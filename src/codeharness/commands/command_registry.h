#pragma once

#include "codeharness/core/error.h"
#include "codeharness/sessions/session_store.h"
#include "codeharness/skills/skill_registry.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codeharness
{

namespace memory
{
class MemoryStore;
}

struct LoadedPlugin;

struct SessionCommandSummary
{
    std::string session_id;
    std::string model;
    std::string summary;
    int message_count = 0;
};

struct BuiltinCommandRegistryOptions
{
    memory::MemoryStore* memory_store = nullptr;
    sessions::SessionStore* session_store = nullptr;
    std::function<absl::StatusOr<SessionCommandSummary>(std::string_view id)> resume_session;
    std::span<const LoadedPlugin> plugins;
};

// slash command result
struct CommandResult
{
    std::optional<std::string> message;       // 直接展示给用户(如 /skills)
    std::optional<std::string> submit_prompt; // 把渲染好的 prompt 投回 agent 循环
    std::optional<std::string> submit_model;  // 换模型
};

// 命令回调:
//   - 入参:args 是去掉 "/name" 前缀的剩余部分(由 lookup 解析)
enum class CommandInvocationKind
{
    Unknown,
    MessageOnly,
    SubmitsPrompt,
};

using CommandHandler = std::function<absl::StatusOr<CommandResult>(std::string_view args)>;

// slash command
struct SlashCommand
{
    std::string name;                 // /help 列表中显示的默认名
    std::string description;          // 命令用途说明
    CommandHandler handler;           // 命令体
    std::vector<std::string> aliases; // 备用名,同样可触发该命令
    CommandInvocationKind invocation = CommandInvocationKind::Unknown;
};

struct CommandLookup
{
    const SlashCommand* command = nullptr; // nullptr 表示未找到
    std::string args;                      // 解析后的参数串
};

// 用 shared_ptr 而非值类型,避免每个 alias 都复制 handler 闭包;
class CommandRegistry
{
public:
    // 注册一个命令:name 和每个 alias 都会进入 map
    // 后注册者覆盖先注册者(同 key 时)
    auto register_command(SlashCommand command) -> void;

    // 解析 input("/name args...")并查找:
    //   - 命中:command 指向 map 中的对象,args 是 Trim 过的剩余部分
    //   - 未命中:command == nullptr,args 被填好
    auto lookup(std::string_view input) const -> CommandLookup;

    // 列出全部去重后的命令(按 name 排序),供 /help 或 dry-run 展示。
    auto list() const -> std::vector<SlashCommand>;

private:
    // 同一个 SlashCommand 实例可以有 name + N 个 alias 共用
    std::unordered_map<std::string, std::shared_ptr<SlashCommand>> by_key_;
};

// 内置 slash command 的注册点。新增内置命令只改这一处,
// handlers 通过 lambda 捕获共用一个 SkillRegistry 引用。
auto build_builtin_command_registry(const SkillRegistry& skills, BuiltinCommandRegistryOptions options = {})
    -> CommandRegistry;

// CLI 用的顶层入口:"解析 + 查找 + 调用"一次完成。
// 未找到命令时返回 InvalidArgument 错误,行为与其它模块的失败约定一致。
auto execute_slash_command(const CommandRegistry& registry, std::string_view input) -> absl::StatusOr<CommandResult>;

} // namespace codeharness

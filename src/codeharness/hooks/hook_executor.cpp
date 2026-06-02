#include "codeharness/hooks/hook_executor.h"

#include <fmt/format.h>
#include <reproc++/reproc.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace codeharness
{

namespace
{

auto matcher_applies(const HookDefinition& hook, const nlohmann::json& payload) -> bool
{
    if (!hook.matcher)
    {
        return true;
    }

    if (auto tool_name = payload.find("tool_name"); tool_name != payload.end() && tool_name->is_string())
    {
        return tool_name->get<std::string>() == *hook.matcher;
    }

    return false;
}

auto unsupported_hook_result(HookType type) -> HookResult
{
    return HookResult{
        .success = false,
        .blocked = false,
        .reason = fmt::format("hook type {} is not implemented yet", static_cast<int>(type)),
    };
}

auto get_shell_prefix() -> std::vector<std::string>
{
#if defined(_WIN32)
    return {"cmd.exe", "/c"};
#else
    return {"/bin/sh", "-c"};
#endif
}

auto append_timeout_message(std::string& output, int timeout_seconds) -> void
{
    if (!output.empty() && output.back() != '\n')
    {
        output += '\n';
    }
    output += fmt::format("[hook command timed out after {} seconds]", timeout_seconds);
}

auto read_command(const HookDefinition& hook) -> std::optional<std::string>
{
    const auto command = hook.config.find("command");
    if (command == hook.config.end() || !command->is_string())
    {
        return std::nullopt;
    }

    return command->get<std::string>();
}

auto read_argv(const HookDefinition& hook) -> std::optional<std::vector<std::string>>
{
    const auto argv_json = hook.config.find("argv");
    if (argv_json == hook.config.end())
    {
        return std::nullopt;
    }

    if (!argv_json->is_array() || argv_json->empty())
    {
        return std::vector<std::string>{};
    }

    std::vector<std::string> argv;
    argv.reserve(argv_json->size());
    for (const auto& value : *argv_json)
    {
        if (!value.is_string())
        {
            return std::vector<std::string>{};
        }
        argv.push_back(value.get<std::string>());
    }

    return argv;
}

auto build_command_argv(const HookDefinition& hook) -> std::optional<std::vector<std::string>>
{
    if (auto argv = read_argv(hook))
    {
        if (argv->empty())
        {
            return std::nullopt;
        }
        return argv;
    }

    const auto command = read_command(hook);
    if (!command || command->empty())
    {
        return std::nullopt;
    }

    auto argv = get_shell_prefix();
    argv.push_back(*command);
    return argv;
}

struct HookTempFile
{
    std::filesystem::path path;
    std::string path_string;

    HookTempFile() = default;
    HookTempFile(const HookTempFile&) = delete;
    auto operator=(const HookTempFile&) -> HookTempFile& = delete;

    HookTempFile(HookTempFile&& other) noexcept : path(std::move(other.path)), path_string(std::move(other.path_string))
    {
        other.path.clear();
    }

    auto operator=(HookTempFile&& other) noexcept -> HookTempFile&
    {
        if (this != &other)
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            path = std::move(other.path);
            path_string = std::move(other.path_string);
            other.path.clear();
        }
        return *this;
    }

    ~HookTempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

auto make_hook_temp_file(std::string_view extension) -> HookTempFile
{
    const auto file_name = fmt::format(
        "codeharness-hook-{}{}",
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count(),
        extension);

    HookTempFile file;
    file.path = std::filesystem::temp_directory_path() / file_name;
    file.path_string = file.path.string();
    return file;
}

auto read_hook_output(const HookTempFile& file) -> std::string
{
    std::ifstream stream{file.path, std::ios::binary};
    if (!stream)
    {
        return {};
    }

    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

auto run_command_hook(const HookDefinition& hook, const nlohmann::json& payload) -> HookResult
{
    auto argv = build_command_argv(hook);
    if (!argv)
    {
        return HookResult{
            .success = false, .reason = "command hook requires string config.command or string array config.argv"};
    }

    const auto payload_json = payload.dump();
    const std::vector<std::pair<std::string, std::string>> environment{
        {"CODEHARNESS_HOOK_PAYLOAD", payload_json},
    };
    auto output_file = make_hook_temp_file(".out");

    reproc::process process;
    reproc::options options;
    options.env.extra = environment;
    options.redirect.in.type = reproc::redirect::discard;
    options.redirect.out.type = reproc::redirect::path_;
    options.redirect.out.path = output_file.path_string.c_str();
    options.redirect.err.type = reproc::redirect::stdout_;

    if (auto error = process.start(*argv, options))
    {
        return HookResult{.success = false, .reason = fmt::format("failed to start hook command: {}", error.message())};
    }

    const auto timeout_seconds = hook.timeout_seconds < 1 ? 1 : hook.timeout_seconds;
    auto [exit_status, wait_error] = process.wait(reproc::milliseconds{timeout_seconds * 1000});
    if (wait_error == std::errc::timed_out)
    {
        process.kill();
        process.wait(reproc::milliseconds{5000});
    }

    auto output = read_hook_output(output_file);
    if (wait_error == std::errc::timed_out)
    {
        append_timeout_message(output, timeout_seconds);
        return HookResult{.success = false, .output = std::move(output), .reason = "hook command timed out"};
    }

    if (wait_error)
    {
        return HookResult{
            .success = false, .reason = fmt::format("failed to wait for hook command: {}", wait_error.message())};
    }

    constexpr std::size_t max_output_length = 12000;
    if (output.size() > max_output_length)
    {
        output.resize(max_output_length);
        output += "\n...[hook output truncated, too long]...";
    }

    if (exit_status != 0)
    {
        return HookResult{
            .success = false,
            .output = std::move(output),
            .reason = fmt::format("hook command exited with code {}", exit_status),
        };
    }

    return HookResult{.success = true, .output = std::move(output)};
}

auto execute_one(const HookDefinition& hook, const nlohmann::json& payload) -> HookResult
{
    if (!matcher_applies(hook, payload))
    {
        return HookResult{.success = true};
    }

    if (hook.type == HookType::Command)
    {
        return run_command_hook(hook, payload);
    }

    if (hook.type != HookType::Callback)
    {
        return unsupported_hook_result(hook.type);
    }

    if (!hook.callback)
    {
        return HookResult{.success = false, .reason = "callback hook has no callback"};
    }

    try
    {
        return hook.callback(payload);
    }
    catch (const std::exception& error)
    {
        return HookResult{.success = false, .reason = error.what()};
    }
}

} // namespace

HookExecutor::HookExecutor(const HookRegistry& registry) : registry_(registry)
{
}

auto HookExecutor::execute(HookEvent event, const nlohmann::json& payload) const -> HookExecutionResult
{
    HookExecutionResult execution;

    for (const auto& hook : registry_.get(event))
    {
        auto result = execute_one(hook, payload);

        const auto blocks = result.blocked || (!result.success && hook.block_on_failure);
        if (blocks)
        {
            execution.blocked = true;
            execution.reason = !result.reason.empty() ? result.reason : "hook blocked execution";
        }

        execution.results.push_back(std::move(result));

        if (execution.blocked)
        {
            break;
        }
    }

    return execution;
}

} // namespace codeharness

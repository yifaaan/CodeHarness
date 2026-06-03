#include "codeharness/tools/bash_tool.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <reproc++/reproc.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include "codeharness/core/assign.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/shell.h"

namespace codeharness
{

namespace
{

struct BashInput
{
    std::string command;       // 要执行的 shell 命令（LLM 生成）
    int timeout_seconds = 600; // 超时秒数，默认 10 分钟
};

// JSON 解析
// LLM 以 JSON 格式调用工具，例如：
//   {"command": "git log --oneline -3", "timeout_seconds": 30}
auto parse_bash_input(const nlohmann::json& input) -> Result<BashInput>
{
    BashInput parsed;

    if (auto r = assign(parsed.command, read_json_field<std::string>(input, "command", "bash")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(
            parsed.timeout_seconds,
            read_json_field<int, JsonFieldMode::optional_with_default>(input, "timeout_seconds", "bash", 600));
        !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (parsed.timeout_seconds < 1)
    {
        parsed.timeout_seconds = 1;
    }
    if (parsed.timeout_seconds > 3600)
    {
        parsed.timeout_seconds = 3600;
    }

    return parsed;
}

auto drain_output(reproc::process& process, std::string& output) -> void
{
    std::array<std::uint8_t, 4096> buf;
    while (true)
    {
        auto [bytes_read, error] = process.read(reproc::stream::out, buf.data(), buf.size());
        if (bytes_read > 0)
        {
            output.append(reinterpret_cast<const char *>(buf.data()), bytes_read);
        }

        if (!error && bytes_read > 0)
        {
            continue;
        }

        if (error && error != std::errc::resource_unavailable_try_again && error != std::errc::operation_would_block &&
            error != std::errc::broken_pipe)
        {
            spdlog::warn("failed to read bash output: {}", error.message());
        }

        break;
    }
}

} // namespace

auto BashTool::name() const -> std::string
{
    return "bash";
}

auto BashTool::description() const -> std::string
{
    return "Execute a shell command. Use this for compilation, testing, "
           "git operations, or any command-line tool. "
           "Input: {\"command\": \"...\", \"timeout_seconds\": 600}";
}

auto BashTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return command_permission_target(request.parsed_input, "command");
}

// ---- 核心执行逻辑 ----
//
// 执行流程（5 步）：
//   ① 解析 JSON → ② 构建参数 → ③ 启动子进程
//   → ④ 等待+超时控制 → ⑤ 收集输出+返回

auto BashTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto parsed = parse_bash_input(request.parsed_input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    spdlog::info("bash command: {}", parsed->command);
    auto argv = default_shell_command_argv(parsed->command);

    reproc::process process;
    reproc::options opts{};

    auto cwd_str = context.cwd.string();
    opts.working_directory = cwd_str.c_str();
    opts.redirect.in.type = reproc::redirect::discard;
    opts.redirect.out.type = reproc::redirect::pipe;
    opts.redirect.err.type = reproc::redirect::stdout_;
    opts.nonblocking = true;

    if (auto error = process.start(argv, opts))
    {
        return fail<ToolResponse>(ErrorKind::Io, fmt::format("failed to start process: {}", error.message()));
    }

    const auto timeout = reproc::milliseconds{parsed->timeout_seconds * 1000};
    auto [exit_status, wait_error] = process.wait(timeout);

    std::string output;

    if (wait_error == std::errc::timed_out)
    {
        drain_output(process, output);

        if (auto error = process.kill())
        {
            spdlog::warn("failed to kill timed-out bash process: {}", error.message());
        }
        process.wait(reproc::milliseconds{5000});

        drain_output(process, output);

        if (!output.empty() && output.back() != '\n')
        {
            output += '\n';
        }
        output += fmt::format("[command timed out after {} seconds]", parsed->timeout_seconds);

        spdlog::warn("bash command timed out after {}s: {}", parsed->timeout_seconds, parsed->command);

        return ToolResponse{
            .tool_use_id = request.id,
            .content = std::move(output),
            .is_error = true,
        };
    }

    if (wait_error)
    {
        return fail<ToolResponse>(ErrorKind::Io, fmt::format("failed to wait for process: {}", wait_error.message()));
    }

    drain_output(process, output);

    constexpr std::size_t max_length = 12000;
    if (output.size() > max_length)
    {
        output.resize(max_length);
        output += "\n...[output truncated, too long]...";
    }

    bool is_error = (exit_status != 0);

    std::string result;
    if (is_error)
    {
        result = fmt::format("Exit code: {}\n\n{}", exit_status, output);
    }
    else
    {
        result = std::move(output);
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = std::move(result),
        .is_error = is_error,
    };
}

} // namespace codeharness

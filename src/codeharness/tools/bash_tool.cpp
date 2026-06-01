#include "codeharness/tools/bash_tool.h"

#include <nlohmann/json.hpp>

#include <reproc/reproc.h>

#include <array>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "codeharness/core/json_parse.h"

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

    auto command = require_string(input, "command", "bash");
    if (!command)
    {
        return fail<BashInput>(command.error().kind, command.error().message);
    }
    parsed.command = std::move(*command);

    auto timeout_seconds = optional_int(input, "timeout_seconds", 600, "bash");
    if (!timeout_seconds)
    {
        return fail<BashInput>(timeout_seconds.error().kind, timeout_seconds.error().message);
    }
    parsed.timeout_seconds = *timeout_seconds;

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

//   Windows: cmd.exe /c "command"
//   Linux:   /bin/sh -c "command"
//
auto get_shell_prefix() -> std::vector<std::string>
{
#if defined(_WIN32)
    // cmd.exe /c 表示 cmd 执行完命令后退出
    return {"cmd.exe", "/c"};
#else
    return {"/bin/sh", "-c"};
#endif
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

    // ========== 构建进程参数 ==========
    //
    // 命令结构（Windows）：
    //   argv[0] = "cmd.exe"        ← shell 程序
    //   argv[1] = "/c"             ← 执行后退出
    //   argv[2] = "echo hello"     ← 原始命令字符串
    //
    // reproc 要求 argv 以 NULL 结尾。

    auto prefix = get_shell_prefix();
    std::vector<const char *> argv;
    for (const auto& part : prefix)
    {
        argv.push_back(part.c_str());
    }
    argv.push_back(parsed->command.c_str());
    argv.push_back(nullptr);

    // ========== 启动子进程 ==========
    //
    // reproc 的启动原理：
    //   1. 内部调用 CreateProcess / fork+exec
    //   2. 根据 options 设置工作目录、管道等
    //   3. 返回后子进程已经开始运行

    auto process = reproc_new();
    if (!process)
    {
        return fail<ToolResponse>(ErrorKind::Io, "failed to create subprocess handle");
    }

    // 配置子进程选项
    reproc_options opts{};

    // 工作目录：设为项目根目录
    // LLM 以相对路径操作文件
    auto cwd_str = context.cwd.string();
    opts.working_directory = cwd_str.c_str();

    // 重定向 stdin 到 /dev/null（不给子进程输入）
    opts.redirect.in.type = REPROC_REDIRECT_DISCARD;
    // 捕获 stdout（通过 pipe 读取子进程输出）
    opts.redirect.out.type = REPROC_REDIRECT_PIPE;
    // stderr 合并到 stdout
    opts.redirect.err.type = REPROC_REDIRECT_STDOUT;

    // 非阻塞模式：read 无数据时立即返回 REPROC_EWOULDBLOCK
    opts.nonblocking = true;

    // 启动
    auto r = reproc_start(process, argv.data(), opts);
    if (r < 0)
    {
        process = reproc_destroy(process);
        return fail<ToolResponse>(ErrorKind::Io, std::format("failed to start process: {}", reproc_strerror(r)));
    }

    // ========== 等待进程结束 + 超时控制 ==========
    //
    // reproc_wait 返回值规则：
    //   0-255              → 子进程退出码（正常退出）
    //   REPROC_ETIMEDOUT   → 超时（子进程还在运行）
    //   < 0                → 系统错误

    int timeout_ms = parsed->timeout_seconds * 1000;
    int exit_status = reproc_wait(process, timeout_ms);

    std::string output;

    if (exit_status == REPROC_ETIMEDOUT)
    {
        // 超时 子进程还在跑，强制杀死。
        //
        // 杀进程的原理：
        //   Windows: TerminateProcess（强制终止）
        //   POSIX:   SIGKILL（不可捕获的终止信号）
        // nonblocking 模式下，无数据时立即返回 REPROC_EWOULDBLOCK
        {
            std::array<uint8_t, 4096> buf;
            while (true)
            {
                int r = reproc_read(process, REPROC_STREAM_OUT, buf.data(), buf.size());
                // reproc_read 返回值规则：
                //   > 0 → 读到 r 字节
                if (r > 0)
                {
                    output.append(reinterpret_cast<const char *>(buf.data()), r);
                }
                else
                {
                    break;
                }
            }
        }

        // 强制杀进程
        reproc_kill(process);
        reproc_wait(process, 5000);

        // 读杀进程后的残留输出
        {
            std::array<uint8_t, 4096> buf;
            while (true)
            {
                int r = reproc_read(process, REPROC_STREAM_OUT, buf.data(), buf.size());
                if (r > 0)
                {
                    output.append(reinterpret_cast<const char *>(buf.data()), r);
                }
                else
                {
                    break;
                }
            }
        }

        // 追加超时信息
        if (!output.empty() && output.back() != '\n')
        {
            output += '\n';
        }
        output += std::format("[command timed out after {} seconds]", parsed->timeout_seconds);

        process = reproc_destroy(process);

        return ToolResponse{
            .tool_use_id = request.id,
            .content = std::move(output),
            .is_error = true,
        };
    }

    // ---- 正常退出 ----
    {
        std::array<uint8_t, 4096> buf;
        while (true)
        {
            int r = reproc_read(process, REPROC_STREAM_OUT, buf.data(), buf.size());
            if (r > 0)
            {
                output.append(reinterpret_cast<const char *>(buf.data()), r);
            }
            else
            {
                break;
            }
        }
    }

    process = reproc_destroy(process);

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
        result = std::format("Exit code: {}\n\n{}", exit_status, output);
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

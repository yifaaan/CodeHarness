#include "codeharness/tools/bash_tool.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <reproc++/reproc.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/shell.h"

namespace codeharness {

namespace {

struct BashInput {
  std::string command;        // 要执行的 shell 命令（LLM 生成）
  int timeout_seconds = 600;  // 超时秒数，默认 10 分钟
};

// JSON 解析
// LLM 以 JSON 格式调用工具，例如：
//   {"command": "git log --oneline -3", "timeout_seconds": 30}
struct BashProcessResult {
  int exit_status = 0;
  bool timed_out = false;
};

constexpr std::size_t max_output_length = 12000;
constexpr std::string_view output_truncated_marker = "\n...[output truncated, too long]...";

auto parse_bash_input(const nlohmann::json& input) -> absl::StatusOr<BashInput> {
  BashInput parsed;

  if (auto r = Assign(parsed.command, ReadJsonField<std::string>(input, "command", "bash")); !r.ok()) {
    return r;
  }

  if (auto r = Assign(parsed.timeout_seconds,
                      ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(input, "timeout_seconds", "bash", 600));
      !r.ok()) {
    return r;
  }

  if (parsed.timeout_seconds < 1) {
    parsed.timeout_seconds = 1;
  }
  if (parsed.timeout_seconds > 3600) {
    parsed.timeout_seconds = 3600;
  }

  return parsed;
}

auto append_output(std::string& output, const std::uint8_t* data, std::size_t size, bool& truncated) -> void {
  if (truncated || size == 0) {
    return;
  }

  const auto remaining = max_output_length > output.size() ? max_output_length - output.size() : std::size_t{0};
  if (size > remaining) {
    output.append(reinterpret_cast<const char*>(data), remaining);
    truncated = true;
    return;
  }

  output.append(reinterpret_cast<const char*>(data), size);
}

auto append_truncation_marker(std::string& output, bool truncated) -> void {
  if (truncated) {
    output += output_truncated_marker;
  }
}

auto drain_available_output(reproc::process& process, std::string& output, bool& truncated) -> void {
  std::array<std::uint8_t, 4096> buf;
  while (true) {
    auto [bytes_read, error] = process.read(reproc::stream::out, buf.data(), buf.size());

    if (!error && bytes_read > 0) {
      if (bytes_read > buf.size()) {
        spdlog::warn("bash read reported {} bytes for a {} byte buffer", bytes_read, buf.size());
        truncated = true;
        break;
      }

      append_output(output, buf.data(), bytes_read, truncated);
      continue;
    }

    if (error && error != std::errc::resource_unavailable_try_again && error != std::errc::operation_would_block &&
        error != std::errc::broken_pipe) {
      spdlog::warn("failed to read bash output: {}", error.message());
    }

    break;
  }
}

auto wait_for_process(reproc::process& process, std::string& output, bool& truncated, int timeout_seconds)
    -> absl::StatusOr<BashProcessResult> {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout_seconds};
  while (true) {
    drain_available_output(process, output, truncated);

    auto [exit_status, wait_error] = process.wait(reproc::milliseconds{0});
    if (!wait_error) {
      drain_available_output(process, output, truncated);
      return BashProcessResult{.exit_status = exit_status};
    }

    if (wait_error != std::errc::timed_out) {
      return absl::InternalError(fmt::format("failed to wait for process: {}", wait_error.message()));
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      drain_available_output(process, output, truncated);

      if (auto error = process.kill()) {
        spdlog::warn("failed to kill timed-out bash process: {}", error.message());
      }
      process.wait(reproc::milliseconds{5000});

      drain_available_output(process, output, truncated);
      return BashProcessResult{.timed_out = true};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
}

}  // namespace

auto BashTool::name() const -> std::string { return "bash"; }

auto BashTool::description() const -> std::string {
  return "Execute a shell command. Use this for compilation, testing, "
         "git operations, or any command-line tool. "
         "Input: {\"command\": \"...\", \"timeout_seconds\": 600}";
}

auto BashTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return command_permission_target(request.parsed_input, "command");
}

// ---- 核心执行逻辑 ----
//
// 执行流程（5 步）：
//   ① 解析 JSON → ② 构建参数 → ③ 启动子进程
//   → ④ 等待+超时控制 → ⑤ 收集输出+返回

auto BashTool::execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> {
  auto parsed = parse_bash_input(request.parsed_input);
  if (!parsed.ok()) {
    return parsed.status();
  }

  spdlog::info("bash command: {}", parsed->command);
  auto argv = DefaultShellCommandArgv(parsed->command);

  reproc::process process;
  reproc::options opts{};

  auto cwd_str = context.cwd.string();
  opts.working_directory = cwd_str.c_str();
  opts.redirect.in.type = reproc::redirect::discard;
  opts.redirect.out.type = reproc::redirect::pipe;
  opts.redirect.err.type = reproc::redirect::stdout_;
  opts.nonblocking = true;

  if (auto error = process.start(argv, opts)) {
    return absl::InternalError(fmt::format("failed to start process: {}", error.message()));
  }

  std::string output;
  bool output_truncated = false;

  auto exit_status = wait_for_process(process, output, output_truncated, parsed->timeout_seconds);
  if (exit_status.ok() && exit_status->timed_out) {
    append_truncation_marker(output, output_truncated);
    if (!output.empty() && output.back() != '\n') {
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

  if (!exit_status.ok()) {
    return exit_status.status();
  }

  append_truncation_marker(output, output_truncated);

  bool is_error = (exit_status->exit_status != 0);

  std::string result;
  if (is_error) {
    result = fmt::format("Exit code: {}\n\n{}", exit_status->exit_status, output);
  } else {
    result = std::move(output);
  }

  return ToolResponse{
      .tool_use_id = request.id,
      .content = std::move(result),
      .is_error = is_error,
  };
}

}  // namespace codeharness

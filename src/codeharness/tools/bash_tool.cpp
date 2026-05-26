#include "codeharness/tools/bash_tool.h"

#include <absl/status/status.h>
#include <absl/strings/ascii.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <reproc++/run.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace codeharness::tools {
    namespace {

        constexpr std::size_t max_output_chars = 12000;

        [[nodiscard]] auto combine_output(std::string stdout_text, std::string stderr_text)
            -> std::string {
            absl::StripTrailingAsciiWhitespace(&stdout_text);
            absl::StripTrailingAsciiWhitespace(&stderr_text);

            auto output = std::string{};

            output += stdout_text;

            if (!output.empty()) {
                output += '\n';
            }
            output += stderr_text;

            absl::StripAsciiWhitespace(&output);
            if (output.empty()) {
                output = "(no output)";
            }
            if (output.size() > max_output_chars) {
                output = output.substr(0, max_output_chars) + "\n...[truncated]...";
            }

            return output;
        }

        [[nodiscard]] auto resolve_working_directory(const std::filesystem::path& base,
                                                     const std::string& requested)
            -> absl::StatusOr<std::filesystem::path> {
            auto cwd = requested.empty() ? base : std::filesystem::path{requested};
            if (cwd.is_relative()) {
                cwd = base / cwd;
            }
            cwd = cwd.lexically_normal();

            std::error_code ec;
            if (!std::filesystem::exists(cwd, ec) || !std::filesystem::is_directory(cwd, ec)) {
                return absl::InvalidArgumentError(
                    absl::StrCat("working directory does not exist: ", cwd.string()));
            }

            return cwd;
        }

        [[nodiscard]] auto shell_arguments(const std::string& command) -> std::vector<std::string> {
#if defined(_WIN32)
            return {"cmd.exe", "/S", "/C", command};
#else
            return {"/bin/bash", "-lc", command};
#endif
        }

        [[nodiscard]] auto execute_shell_command(const std::string& command,
                                                 const std::filesystem::path& cwd,
                                                 int timeout_seconds)
            -> absl::StatusOr<std::string> {
            auto stdout_text = std::string{};
            auto stderr_text = std::string{};

            const auto cwd_string = cwd.string();
            auto options = reproc::options{};
            options.working_directory = cwd_string.c_str();
            options.redirect.in.type = reproc::redirect::discard;
            options.redirect.out.type = reproc::redirect::pipe;
            options.redirect.err.type = reproc::redirect::pipe;
            options.deadline = reproc::milliseconds{timeout_seconds * 1000};

            const auto [exit_status, error] =
                reproc::run(shell_arguments(command), options, reproc::sink::string{stdout_text},
                            reproc::sink::string{stderr_text});

            if (error == std::errc::timed_out) {
                return absl::DeadlineExceededError(
                    absl::StrCat("Command timed out after ", timeout_seconds, " seconds"));
            }
            if (error) {
                return absl::InternalError(
                    absl::StrCat("failed to run command: ", error.message()));
            }

            auto output = combine_output(std::move(stdout_text), std::move(stderr_text));
            if (exit_status != 0) {
                return absl::InternalError(
                    absl::StrCat(output, "\n(returncode: ", exit_status, ")"));
            }

            return output;
        }

    }  // namespace

    auto BashTool::name() const -> absl::string_view { return "bash"; }

    auto BashTool::description() const -> absl::string_view {
        return "Run a shell command in the local repository.";
    }

    auto BashTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"command", {{"type", "string"}, {"description", "Shell command to execute."}}},
                 {"cwd",
                  {{"type", "string"}, {"description", "Optional working directory override."}}},
                 {"timeout_seconds",
                  {{"type", "integer"}, {"default", 120}, {"minimum", 1}, {"maximum", 600}}},
             }},
            {"required", {"command"}},
            {"additionalProperties", false},
        };
    }

    auto BashTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto BashTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto command = input.at("command").get<std::string>();
        const auto cwd_arg = input.value("cwd", std::string{});
        const auto timeout_seconds = input.value("timeout_seconds", 120);

        if (command.empty()) {
            return absl::InvalidArgumentError("command must not be empty");
        }
        if (timeout_seconds < 1 || timeout_seconds > 600) {
            return absl::InvalidArgumentError("timeout_seconds must be between 1 and 600");
        }

        const auto cwd = resolve_working_directory(ctx.cwd, cwd_arg);
        if (!cwd.ok()) {
            return cwd.status();
        }

        return execute_shell_command(command, *cwd, timeout_seconds);
    }

}  // namespace codeharness::tools

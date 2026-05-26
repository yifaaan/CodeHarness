#include "codeharness/tools/remote_trigger_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <string>

#include "codeharness/services/cron.h"
#include "codeharness/tools/bash_tool.h"

namespace codeharness::tools {

    auto RemoteTriggerTool::name() const -> absl::string_view { return "remote_trigger"; }

    auto RemoteTriggerTool::description() const -> absl::string_view {
        return "Trigger a configured local cron-style job immediately.";
    }

    auto RemoteTriggerTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"name", {{"type", "string"}, {"description", "Cron job name."}}},
                 {"timeout_seconds",
                  {{"type", "integer"}, {"default", 120}, {"minimum", 1}, {"maximum", 600}}},
             }},
            {"required", {"name"}},
            {"additionalProperties", false},
        };
    }

    auto RemoteTriggerTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto RemoteTriggerTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto name = input.at("name").get<std::string>();
        const auto timeout_seconds = input.value("timeout_seconds", 120);

        if (name.empty()) {
            return absl::InvalidArgumentError("name must not be empty");
        }
        if (timeout_seconds < 1 || timeout_seconds > 600) {
            return absl::InvalidArgumentError("timeout_seconds must be between 1 and 600");
        }

        const auto job = services::cron::get_cron_job(name);
        if (!job.has_value()) {
            return absl::NotFoundError(absl::StrCat("Cron job not found: ", name));
        }

        auto bash = BashTool{};
        const auto cwd = job->cwd.empty() ? ctx.cwd.string() : job->cwd.string();
        const auto output = bash.execute(
            nlohmann::json{
                {"command", job->command},
                {"cwd", cwd},
                {"timeout_seconds", timeout_seconds},
            },
            ctx);
        if (!output.ok()) {
            return output.status();
        }
        return absl::StrCat("Triggered ", name, "\n", *output);
    }

}  // namespace codeharness::tools

#include "codeharness/tools/cron_create_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <string>

#include "codeharness/services/cron.h"

namespace codeharness::tools {

    auto CronCreateTool::name() const -> absl::string_view { return "cron_create"; }

    auto CronCreateTool::description() const -> absl::string_view {
        return "Create or replace a local cron-style job.";
    }

    auto CronCreateTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"name", {{"type", "string"}, {"description", "Unique cron job name."}}},
                 {"schedule", {{"type", "string"}, {"description", "Human-readable schedule expression."}}},
                 {"command", {{"type", "string"}, {"description", "Shell command to run when triggered."}}},
                 {"cwd", {{"type", "string"}, {"description", "Optional working directory override."}}},
             }},
            {"required", {"name", "schedule", "command"}},
            {"additionalProperties", false},
        };
    }

    auto CronCreateTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto CronCreateTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto name = input.at("name").get<std::string>();
        const auto schedule = input.at("schedule").get<std::string>();
        const auto command = input.at("command").get<std::string>();
        const auto cwd = input.value("cwd", ctx.cwd.string());

        if (name.empty()) {
            return absl::InvalidArgumentError("name must not be empty");
        }
        if (schedule.empty()) {
            return absl::InvalidArgumentError("schedule must not be empty");
        }
        if (command.empty()) {
            return absl::InvalidArgumentError("command must not be empty");
        }

        const auto status = services::cron::upsert_cron_job(services::cron::CronJob{
            .name = name,
            .schedule = schedule,
            .command = command,
            .cwd = std::filesystem::path{cwd},
        });
        if (!status.ok()) {
            return status;
        }
        return absl::StrCat("Created cron job ", name);
    }

}  // namespace codeharness::tools

#include "codeharness/tools/cron_list_tool.h"

#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <string>

#include "codeharness/services/cron.h"

namespace codeharness::tools {

    auto CronListTool::name() const -> absl::string_view { return "cron_list"; }

    auto CronListTool::description() const -> absl::string_view {
        return "List configured local cron-style jobs.";
    }

    auto CronListTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"additionalProperties", false},
        };
    }

    auto CronListTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto CronListTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(input);
        static_cast<void>(ctx);

        const auto jobs = services::cron::load_cron_jobs();
        if (jobs.empty()) {
            return "No cron jobs configured.";
        }

        auto output = std::string{};
        for (const auto& job : jobs) {
            if (!output.empty()) {
                output += '\n';
            }
            absl::StrAppend(&output, job.name, " [", job.schedule, "] -> ", job.command);
        }
        return output;
    }

}  // namespace codeharness::tools

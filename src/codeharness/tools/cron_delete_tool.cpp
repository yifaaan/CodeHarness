#include "codeharness/tools/cron_delete_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <string>

#include "codeharness/services/cron.h"

namespace codeharness::tools {

    auto CronDeleteTool::name() const -> absl::string_view { return "cron_delete"; }

    auto CronDeleteTool::description() const -> absl::string_view {
        return "Delete a local cron-style job by name.";
    }

    auto CronDeleteTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"name", {{"type", "string"}, {"description", "Cron job name."}}},
             }},
            {"required", {"name"}},
            {"additionalProperties", false},
        };
    }

    auto CronDeleteTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto CronDeleteTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto name = input.at("name").get<std::string>();
        if (name.empty()) {
            return absl::InvalidArgumentError("name must not be empty");
        }

        const auto deleted = services::cron::delete_cron_job(name);
        if (!deleted.ok()) {
            return deleted.status();
        }
        if (!*deleted) {
            return absl::NotFoundError(absl::StrCat("Cron job not found: ", name));
        }
        return absl::StrCat("Deleted cron job ", name);
    }

}  // namespace codeharness::tools

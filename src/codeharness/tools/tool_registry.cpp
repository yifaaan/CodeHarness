#include "codeharness/tools/tool_registry.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>

#include "codeharness/logging.h"
#include "codeharness/tools/base.h"

namespace codeharness::tools {
    auto ToolRegistry::register_tool(std::unique_ptr<Tool> item) -> void {
        auto tool_name = item->name();
        CH_LOG_DEBUG("ToolRegistry::register_tool", "tool_name={}", tool_name);
        tools_.insert_or_assign(std::string{tool_name}, std::move(item));
    }

    auto ToolRegistry::find(absl::string_view name) const -> absl::StatusOr<tools::Tool*> {
        if (const auto it = tools_.find(name); it != tools_.end()) {
            CH_LOG_DEBUG("ToolRegistry::find", "lookup hit tool_name={}", name);
            return it->second.get();
        } else {
            CH_LOG_DEBUG("ToolRegistry::find", "lookup miss tool_name={}", name);
            return absl::NotFoundError(absl::StrCat("tool not found: ", name));
        }
    }

    auto ToolRegistry::list_tools() const -> std::vector<const Tool*> {
        std::vector<const Tool*> result;
        result.reserve(tools_.size());
        for (auto& [_, item] : tools_) {
            result.emplace_back(item.get());
        }
        return result;
    }

    auto ToolRegistry::api_schema() const -> nlohmann::json {
        auto result = nlohmann::json::array();
        result.get_ref<nlohmann::json::array_t&>().reserve(tools_.size());
        for (auto& [_, item] : tools_) {
            result.emplace_back(item->api_schema());
        }
        CH_LOG_DEBUG("ToolRegistry::api_schema", "schema_count={}", result.size());
        return result;
    }
}  // namespace codeharness::tools

#include "codeharness/tools/tool_registry.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <spdlog/spdlog.h>

#include "codeharness/tools/base.h"

namespace codeharness::tools {
    auto ToolRegistry::register_tool(std::unique_ptr<Tool> item) -> absl::Status {
        if (!item) {
            return absl::InvalidArgumentError("cannot register null tool");
        }

        auto tool_name = item->name();
        if (tool_name.empty()) {
            return absl::InvalidArgumentError("tool name cannot be empty");
        }
        spdlog::debug("tools: registering tool name={}", tool_name);
        tools_.insert_or_assign(std::string{tool_name}, std::move(item));
        return absl::OkStatus();
    }

    auto ToolRegistry::find(absl::string_view name) const -> absl::StatusOr<tools::Tool*> {
        if (const auto it = tools_.find(name); it != tools_.end()) {
            spdlog::debug("tools: lookup hit name={}", name);
            return it->second.get();
        } else {
            spdlog::debug("tools: lookup miss name={}", name);
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
        spdlog::debug("tools: built api schema count={}", result.size());
        return result;
    }
}  // namespace codeharness::tools

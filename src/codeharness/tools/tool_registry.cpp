#include "codeharness/tools/tool_registry.h"

#include "codeharness/tools/base.h"

namespace codeharness::tools {
    auto ToolRegistry::register_tool(std::unique_ptr<Tool> item) -> void {
        if (!item) {
            throw std::invalid_argument{"cannot register null tool"};
        }

        auto tool_name = item->name();
        if (tool_name.empty()) {
            throw std::invalid_argument{"tool name cannot be empty"};
        }
        tools_.insert_or_assign(tool_name, std::move(item));
    }

    auto ToolRegistry::find(absl::string_view name) const -> tools::Tool* {
        auto it = tools_.find(name);
        if (it == tools_.end()) {
            return nullptr;
        }
        return it->second.get();
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
        for (auto& [_, item] : tools_) {
            result.emplace_back(item->api_schema());
        }
        return result;
    }
}  // namespace codeharness::tools
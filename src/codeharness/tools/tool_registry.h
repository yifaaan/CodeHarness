#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <memory>
#include <nlohmann/json.hpp>

#include "codeharness/tools/base.h"

namespace codeharness::tools {

    // 工具注册表
    class ToolRegistry {
    public:
        ToolRegistry() = default;
        ~ToolRegistry() = default;
        ToolRegistry(const ToolRegistry&) = delete;
        ToolRegistry& operator=(const ToolRegistry&) = delete;
        ToolRegistry(ToolRegistry&&) = default;
        ToolRegistry& operator=(ToolRegistry&&) = default;

        auto register_tool(std::unique_ptr<Tool> item) -> absl::Status;

        [[nodiscard]] auto find(absl::string_view name) const -> absl::StatusOr<tools::Tool*>;
        // 生成发给模型 API 的工具 schema 列表
        [[nodiscard]] auto api_schema() const -> nlohmann::json;
        [[nodiscard]] auto list_tools() const -> std::vector<const Tool*>;

    private:
        absl::flat_hash_map<std::string, std::unique_ptr<Tool>> tools_;
    };

}  // namespace codeharness::tools

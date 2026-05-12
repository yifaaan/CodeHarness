#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/strings/string_view.h>

#include <memory>
#include <nlohmann/json.hpp>

namespace codeharness::tools {

    class Tool;
    // 工具注册表
    class ToolRegistry {
    public:
        auto register_tool(std::unique_ptr<Tool> item) -> void;

        [[nodiscard]] auto find(absl::string_view name) const -> tools::Tool*;
        // 生成发给模型 API 的工具 schema 列表
        [[nodiscard]] auto api_schema() const -> nlohmann::json;
        [[nodiscard]] auto list_tools() const -> std::vector<const Tool*>;

    private:
        absl::flat_hash_map<std::string, std::unique_ptr<Tool>> tools_;
    };

}  // namespace codeharness::tools
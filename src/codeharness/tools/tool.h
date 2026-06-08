#pragma once

#include "codeharness/core/result.h"
#include "codeharness/tools/permission_target.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace codeharness
{

struct ToolContext
{
    std::filesystem::path cwd;
    // 当前 agent 的身份标识（如 task_id）。Engine 在调用工具前自动注入，
    // 工具实现可以此获知"谁在调用我"。目前 SendMessageTool 用它自动填充
    // sender_id。
    std::string sender_id;
};

struct ToolRequest
{
    std::string id;
    std::string name;
    std::string input_json;
    // 已解析的 input_json；permission_target 与 execute 共用以避免重复 parse。
    // 缺失表示尚未解析或 JSON 非法（由 execute 自己处理）。
    nlohmann::json parsed_input;
};

struct ToolResponse
{
    std::string tool_use_id;
    std::string content;
    bool is_error = false;
};

class Tool
{
public:
    virtual ~Tool() = default;

    virtual auto name() const -> std::string = 0;
    virtual auto description() const -> std::string = 0;

    // 工具是否只读。
    virtual auto is_read_only() const noexcept -> bool
    {
        return false;
    }

    // 工具作用在哪个权限目标上。
    virtual auto permission_target(const ToolRequest&) const -> PermissionTarget
    {
        return {};
    }

    virtual auto execute(const ToolRequest& request, [[maybe_unused]] const ToolContext& context) const -> Result<ToolResponse> = 0;
};

// 解析 tool request 的 input_json：若 request.parsed_input 已填充则原样返回；
// 否则尝试 parse input_json 并写入 request.parsed_input。失败时返回错误。
auto parse_tool_request_input(ToolRequest& request, std::string_view tool_name) -> Result<nlohmann::json*>;

} // namespace codeharness

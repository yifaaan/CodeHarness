#pragma once

#include "codeharness/core/result.h"

#include <filesystem>
#include <string>

namespace codeharness
{

struct ToolContext
{
    std::filesystem::path cwd;
};

struct ToolRequest
{
    std::string id;
    std::string name;
    std::string input_json;
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

    virtual auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> = 0;
};

} // namespace codeharness
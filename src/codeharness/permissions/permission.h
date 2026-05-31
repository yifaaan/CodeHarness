#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness
{

//   Default  : 只读工具自动允许，写操作需要确认
//   Plan     : 只允许读取，禁止修改
//   FullAuto : 普通操作自动允许，但敏感路径仍然硬拒绝
enum class PermissionMode
{
    Default,
    Plan,
    FullAuto
};


//   - allowed_tools / denied_tools 使用精确工具名匹配
//   - TODO: wildcard、path rules、command rules
struct PermissionSettings
{
    PermissionMode mode = PermissionMode::Default;

    std::vector<std::string> allowed_tools;
    std::vector<std::string> denied_tools;
};

// 权限判断结果。
// 
// Allow：可以直接执行
// Ask  ：需要用户确认
// Deny ：直接拒绝
enum class PermissionAction
{
    Allow,
    Ask,
    Deny
};

struct PermissionDecision
{
    PermissionAction action = PermissionAction::Deny;
    std::string reason;

    auto is_allowed() const noexcept -> bool
    {
        return action == PermissionAction::Allow;
    }

    auto needs_confirmation() const noexcept -> bool
    {
        return action == PermissionAction::Ask;
    }
};

class PermissionChecker
{
public:
    explicit PermissionChecker(PermissionSettings settings);

    // target_path:
    //   工具作用的路径。没有路径的工具可以传 std::nullopt。
    //
    // command:
    //   bash 这类命令工具的命令字符串。非命令工具传 std::nullopt。
    auto evaluate(
        std::string_view tool_name,
        bool is_read_only,
        std::optional<std::filesystem::path> target_path,
        std::optional<std::string> command) const -> PermissionDecision;

private:
    PermissionSettings settings_;
};

} // namespace codeharness
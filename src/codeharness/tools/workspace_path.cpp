#include "codeharness/tools/workspace_path.h"

#include <system_error>

namespace codeharness
{

auto is_under_directory(const std::filesystem::path& base, const std::filesystem::path& target) -> bool
{
    std::error_code error;

    // relative(target, base) 的意思是：
    //   从 base 出发，怎样走到 target？
    //
    // 如果 target 在 base 内部，例如：
    //   base   = D:/repo
    //   target = D:/repo/src/main.cpp
    //
    // relative 结果就是：
    //   src/main.cpp
    //
    // 如果 target 在 base 外部，例如：
    //   base   = D:/repo
    //   target = D:/outside.txt
    //
    // relative 结果可能包含：
    //   ../outside.txt
    auto relative = std::filesystem::relative(target, base, error);

    if (error)
    {
        return false;
    }

    // target == base 时，relative 为空。
    if (relative.empty())
    {
        return false;
    }

    if (relative.is_absolute())
    {
        return false;
    }

    // relative 里面出现 ".."，就说明 target 需要向上跳出 base 路径逃逸。
    for (const auto& part : relative)
    {
        if (part == "..")
        {
            return false;
        }
    }

    return true;
}

auto resolve_workspace_path(const std::filesystem::path& cwd, const std::filesystem::path& requested)
    -> Result<std::filesystem::path>
{
    if (requested.empty())
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "path is empty");
    }

    // 工具层不接受绝对路径。
    //
    //   模型应该只能操作当前 workspace 内的文件。
    if (requested.is_absolute())
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "absolute paths are not allowed");
    }

    std::error_code error;

    // 先规范化 cwd。
    //
    // weakly_canonical 会处理：
    //   - .
    //   - ..
    //   - 符号链接
    //
    auto base = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve workspace path: " + error.message());
    }

    // 把用户路径拼到 workspace 后面，再规范化。
    //
    // 例如：
    //   cwd       = D:/repo
    //   requested = src/../README.md
    //
    // 得到：
    //   D:/repo/README.md
    auto target = std::filesystem::weakly_canonical(base / requested, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve path: " + error.message());
    }

    if (!is_under_directory(base, target))
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "path escapes cwd");
    }

    return target;
}

} // namespace codeharness
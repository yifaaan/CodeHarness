#include "codeharness/prompts/project_context.h"

#include <nonstd/expected.hpp>

#include <algorithm>
#include <system_error>
#include <utility>

#include "codeharness/tools/text_file.h"

namespace codeharness
{

namespace
{

// 从当前工作目录 cwd 开始，向上找到项目边界，然后生成一组 要搜索上下文文件的目录
auto collect_search_dirs(const std::filesystem::path& cwd) -> Result<std::vector<std::filesystem::path>>
{
    std::error_code error;
    auto current = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::vector<std::filesystem::path>>(ErrorKind::Io, "failed to resolve cwd: " + error.message());
    }

    if (!std::filesystem::is_directory(current, error))
    {
        return fail<std::vector<std::filesystem::path>>(ErrorKind::InvalidArgument, "cwd is not a directory");
    }

    std::vector<std::filesystem::path> dirs;

    while (true)
    {
        dirs.push_back(current);

        if (std::filesystem::exists(current / ".git", error))
        {
            break;
        }

        auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            break;
        }

        current = parent;
    }

    std::reverse(dirs.begin(), dirs.end());
    return dirs;
}

} // namespace

auto load_project_context_files(const std::filesystem::path& cwd, ProjectContextOptions options)
    -> Result<std::vector<ContextFile>>
{
    if (options.max_total_chars == 0 || options.file_names.empty())
    {
        return std::vector<ContextFile>{};
    }

    auto dirs = collect_search_dirs(cwd);
    if (!dirs)
    {
        return nonstd::make_unexpected(dirs.error());
    }

    std::vector<ContextFile> files;
    auto remaining_chars = options.max_total_chars;

    for (const auto& dir : *dirs)
    {
        for (const auto& file_name : options.file_names)
        {
            std::error_code error;
            auto candidate = dir / file_name;
            if (!std::filesystem::is_regular_file(candidate, error))
            {
                continue;
            }

            auto content = codeharness::read_text_file(candidate);
            if (!content)
            {
                return nonstd::make_unexpected(content.error());
            }

            if (content->size() > remaining_chars)
            {
                content->resize(remaining_chars);
                remaining_chars = 0;
            }
            else
            {
                remaining_chars -= content->size();
            }

            files.push_back(
                ContextFile{
                    .path = candidate,
                    .content = std::move(*content),
                });

            if (remaining_chars == 0)
            {
                return files;
            }
        }
    }

    return files;
}

} // namespace codeharness

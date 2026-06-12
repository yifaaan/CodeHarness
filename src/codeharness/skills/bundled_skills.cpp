#include "codeharness/skills/bundled_skills.h"

#include <fmt/format.h>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "codeharness/skills/skill_loader.h"
#include "codeharness/tools/text_file.h"

namespace codeharness
{

namespace
{

constexpr auto BUNDLED_SKILL_ENV = "CODEHARNESS_BUNDLED_SKILL_DIR";

auto environment_bundled_skill_dir() -> std::optional<std::filesystem::path>
{
    auto value = std::getenv(BUNDLED_SKILL_ENV);
    if (value == nullptr || *value == '\0')
    {
        return std::nullopt;
    }

    return std::filesystem::path{value};
}

auto source_tree_bundled_skill_dir() -> std::filesystem::path
{
#ifdef CODEHARNESS_SOURCE_DIR
    // 由构建系统注入源码根目录。这样即使 CLI 先执行 --cwd 切到
    // 其它项目,默认 bundled skills 仍然能从当前 CodeHarness 源码树加载。
    return std::filesystem::path{CODEHARNESS_SOURCE_DIR} / "src" / "codeharness" / "skills" / "bundled" / "content";
#else
    // __FILE__ 通常以相对源码路径传给编译器:
    //   src/codeharness/skills/bundled_skills.cpp
    return std::filesystem::path{__FILE__}.parent_path() / "bundled" / "content";
#endif
}

auto find_bundled_skill_dir_from_cwd(const std::filesystem::path& relative_source_dir) -> std::filesystem::path
{
    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (error)
    {
        return relative_source_dir;
    }

    // 允许 `codeharness --cwd <repo/subdir>` 这类从仓库子目录运行的开发场景:
    // 从当前目录逐层向上找 src/codeharness/skills/bundled/content。
    while (true)
    {
        const auto candidate = current / relative_source_dir;
        if (std::filesystem::is_directory(candidate, error))
        {
            return candidate;
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            return relative_source_dir;
        }

        current = parent;
    }
}

auto make_bundled_skill(const std::filesystem::path& path, std::string content) -> SkillDefinition
{
    // 上游 bundled skill 是扁平文件布局:
    //   content/review.md
    //
    // C++ 的 user/project skill 则是目录布局:
    //   skills/review/SKILL.md
    auto skill = parse_skill_markdown(path.stem().string(), std::move(content), "bundled");
    skill.path = path;
    skill.base_dir = path.parent_path();
    skill.command_name = path.stem().string();

    if (skill.name != *skill.command_name)
    {
        skill.display_name = skill.name;
    }

    return skill;
}

} // namespace

auto default_bundled_skill_dir() -> std::filesystem::path
{
    if (const auto from_env = environment_bundled_skill_dir())
    {
        return *from_env;
    }

    auto source_dir = source_tree_bundled_skill_dir();
    if (source_dir.is_absolute())
    {
        return source_dir;
    }

    return find_bundled_skill_dir_from_cwd(source_dir);
}

auto load_bundled_skills_from_dir(const std::filesystem::path& content_dir) -> absl::StatusOr<std::vector<SkillDefinition>>
{
    std::error_code error;
    if (!std::filesystem::is_directory(content_dir, error))
    {
        return fail<std::vector<SkillDefinition>>(
            absl::InternalError , fmt::format("bundled skill directory does not exist: {}", content_dir.string()));
    }

    std::vector<std::filesystem::path> files;
    std::filesystem::directory_iterator it{content_dir, error};
    const std::filesystem::directory_iterator end;

    for (; !error && it != end; it.increment(error))
    {
        if (it->is_regular_file(error) && it->path().extension() == ".md")
        {
            files.push_back(it->path());
        }
    }

    if (error)
    {
        return fail<std::vector<SkillDefinition>>(
            absl::InternalError ,
            fmt::format("failed to scan bundled skill directory {}: {}", content_dir.string(), error.message()));
    }

    std::ranges::sort(files);

    std::vector<SkillDefinition> skills;
    skills.reserve(files.size());

    for (const auto& path : files)
    {
        auto content = ReadTextFile(path);
        if (!content)
        {
            return content.error();
        }

        skills.push_back(make_bundled_skill(path, std::move(*content)));
    }

    return skills;
}

auto default_bundled_skills() -> absl::StatusOr<std::vector<SkillDefinition>>
{
    return load_bundled_skills_from_dir(default_bundled_skill_dir());
}

} // namespace codeharness

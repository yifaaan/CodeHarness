#include "codeharness/skills/skill_loader.h"

#include <glob/glob.h>
#include <nonstd/expected.hpp>
#include <yaml-cpp/yaml.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "codeharness/core/strings.h"
#include "codeharness/skills/skill_yaml.h"
#include "codeharness/tools/text_file.h"

namespace codeharness
{

namespace
{

using skills::yaml_get_bool;
using skills::yaml_get_string;
using skills::yaml_get_string_list;

struct ParsedSkillMarkdown
{
    YAML::Node frontmatter;
    std::string_view body;
};

auto parse_frontmatter(std::string_view content) -> ParsedSkillMarkdown
{
    auto [first_line, offset] = next_line(content, 0);
    if (trim(first_line) != "---")
    {
        return ParsedSkillMarkdown{.body = content};
    }

    const auto frontmatter_start = offset;

    while (offset < content.size())
    {
        const auto line_start = offset;
        auto [line, next_offset] = next_line(content, offset);
        offset = next_offset;

        if (trim(line) == "---")
        {
            const auto raw_yaml = content.substr(frontmatter_start, line_start - frontmatter_start);
            YAML::Node metadata;
            try
            {
                metadata = YAML::Load(std::string{raw_yaml});
            }
            catch (const YAML::Exception&)
            {
                metadata = YAML::Node{};
            }

            return ParsedSkillMarkdown{
                .frontmatter = std::move(metadata),
                .body = content.substr(offset),
            };
        }
    }

    return ParsedSkillMarkdown{.body = content};
}

auto description_from_body(std::string_view body, std::string& name, std::string_view default_name) -> std::string
{
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;
        const auto stripped = trim(line);

        if (stripped.starts_with("# "))
        {
            if (name == default_name)
            {
                const auto heading = trim(stripped.substr(2));
                if (!heading.empty())
                {
                    name = heading;
                }
            }
            continue;
        }

        if (!stripped.empty() && !stripped.starts_with('#'))
        {
            return std::string{stripped.substr(0, 200)};
        }
    }

    return "Skill: " + name;
}

auto has_parent_reference(const std::filesystem::path& path) -> bool
{
    return std::ranges::any_of(path, [](const auto& part) { return part == ".."; });
}

auto is_safe_project_skill_dir(const std::filesystem::path& path) -> bool
{
    return !path.empty() && !path.is_absolute() && !path.has_root_name() && !has_parent_reference(path);
}

auto find_git_root(const std::filesystem::path& start) -> std::optional<std::filesystem::path>
{
    auto current = start;

    while (true)
    {
        std::error_code error;
        if (std::filesystem::exists(current / ".git", error))
        {
            return current;
        }

        const auto parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            return std::nullopt;
        }

        current = parent;
    }
}

auto home_directory() -> std::optional<std::filesystem::path>
{
#ifdef _WIN32
    const auto* home = std::getenv("USERPROFILE");
#else
    const auto* home = std::getenv("HOME");
#endif

    if (home == nullptr || *home == '\0')
    {
        return std::nullopt;
    }

    return std::filesystem::path{home};
}

} // namespace

auto default_user_skill_dirs() -> std::vector<std::filesystem::path>
{
    const auto home = home_directory();
    if (!home)
    {
        return {};
    }

    return {
        *home / ".codeharness" / "skills",
        *home / ".claude" / "skills",
        *home / ".agents" / "skills",
    };
}

auto parse_skill_markdown(std::string_view default_name, std::string content, std::string source) -> SkillDefinition
{
    const auto parsed = parse_frontmatter(content);
    auto name = yaml_get_string(parsed.frontmatter, "name").value_or(std::string{default_name});
    auto description = yaml_get_string(parsed.frontmatter, "description").value_or(std::string{});

    if (description.empty())
    {
        description = description_from_body(parsed.body, name, default_name);
    }

    return SkillDefinition{
        .name = std::move(name),
        .description = std::move(description),
        .content = std::move(content),
        .source = std::move(source),
        .aliases = yaml_get_string_list(parsed.frontmatter, "aliases"),
        .user_invocable = yaml_get_bool(parsed.frontmatter, "user-invocable", true),
        .disable_model_invocation = yaml_get_bool(parsed.frontmatter, "disable-model-invocation", false),
        .model = yaml_get_string(parsed.frontmatter, "model"),
        .argument_hint = yaml_get_string(parsed.frontmatter, "argument-hint"),
    };
}

auto load_skill_file(const std::filesystem::path& path, std::string source) -> Result<SkillDefinition>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    // skill 目录名作为default name
    auto skill = parse_skill_markdown(path.parent_path().filename().string(), std::move(*content), std::move(source));
    skill.path = path;
    skill.base_dir = path.parent_path();
    skill.command_name = path.parent_path().filename().string();

    if (skill.name != *skill.command_name)
    {
        skill.display_name = skill.name;
    }

    return skill;
}

auto load_skills_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> Result<std::vector<SkillDefinition>>
{
    std::vector<SkillDefinition> skills;
    std::set<std::filesystem::path> seen;

    for (const auto& root : directories)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            continue;
        }

        std::vector<std::filesystem::path> matches;
        try
        {
            matches = glob::glob(root.string() + "/*/SKILL.md");
        }
        catch (const std::exception& e)
        {
            return fail<std::vector<SkillDefinition>>(
                ErrorKind::Io,
                fmt::format("failed to scan skill directory {}: {}", root.string(), e.what()));
        }

        std::ranges::sort(matches);

        for (const auto& candidate : matches)
        {
            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            if (error)
            {
                return fail<std::vector<SkillDefinition>>(
                    ErrorKind::Io,
                    fmt::format("failed to resolve skill path {}: {}", candidate.string(), error.message()));
            }

            if (!seen.emplace(canonical).second)
            {
                continue;
            }

            auto skill = load_skill_file(canonical, std::string{source});
            if (!skill)
            {
                return nonstd::make_unexpected(skill.error());
            }

            skills.push_back(std::move(*skill));
        }
    }

    return skills;
}


// 从 cwd 一路向上，收集到 git_root 为止的每一层
auto discover_project_skill_dirs(const std::filesystem::path& cwd,
                                 std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>
{
    std::error_code error;
    auto start = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::vector<std::filesystem::path>>(
            ErrorKind::Io, fmt::format("failed to resolve cwd: {}", error.message()));
    }

    if (!std::filesystem::is_directory(start, error))
    {
        return fail<std::vector<std::filesystem::path>>(ErrorKind::InvalidArgument, "cwd is not a directory");
    }

    std::vector<std::filesystem::path> levels;
    auto current = start;
    const auto git_root = find_git_root(start);

    while (true)
    {
        levels.push_back(current);
        if (!git_root || current == *git_root)
        {
            break;
        }

        current = current.parent_path();
    }

    std::ranges::reverse(levels);

    std::vector<std::filesystem::path> roots;
    std::set<std::filesystem::path> seen;

    for (const auto& level : levels)
    {
        for (const auto& relative_dir : relative_dirs)
        {
            if (!is_safe_project_skill_dir(relative_dir))
            {
                continue;
            }

            const auto candidate = level / relative_dir;
            if (!std::filesystem::is_directory(candidate, error))
            {
                continue;
            }

            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            if (error)
            {
                return fail<std::vector<std::filesystem::path>>(
                    ErrorKind::Io,
                    fmt::format("failed to resolve skill directory {}: {}", candidate.string(), error.message()));
            }

            if (seen.emplace(canonical).second)
            {
                roots.push_back(canonical);
            }
        }
    }

    return roots;
}

auto load_skill_registry(const std::filesystem::path& cwd, SkillLoadOptions options) -> Result<SkillRegistry>
{
    SkillRegistry registry;

    for (auto& skill : options.bundled_skills)
    {
        registry.register_skill(std::move(skill));
    }

    auto user_skill_dirs = std::move(options.user_skill_dirs);
    if (options.load_default_user_skills)
    {
        auto defaults = default_user_skill_dirs();
        user_skill_dirs.insert(user_skill_dirs.begin(), defaults.begin(), defaults.end());
    }

    auto user_skills = load_skills_from_dirs(user_skill_dirs, "user");
    if (!user_skills)
    {
        return nonstd::make_unexpected(user_skills.error());
    }

    for (auto& skill : *user_skills)
    {
        registry.register_skill(std::move(skill));
    }

    auto extra_skills = load_skills_from_dirs(options.extra_skill_dirs, "user");
    if (!extra_skills)
    {
        return nonstd::make_unexpected(extra_skills.error());
    }

    for (auto& skill : *extra_skills)
    {
        registry.register_skill(std::move(skill));
    }

    if (options.allow_project_skills)
    {
        auto project_dirs = discover_project_skill_dirs(cwd, options.project_skill_dirs);
        if (!project_dirs)
        {
            return nonstd::make_unexpected(project_dirs.error());
        }

        auto project_skills = load_skills_from_dirs(*project_dirs, "project");
        if (!project_skills)
        {
            return nonstd::make_unexpected(project_skills.error());
        }

        for (auto& skill : *project_skills)
        {
            registry.register_skill(std::move(skill));
        }
    }

    return registry;
}

} // namespace codeharness

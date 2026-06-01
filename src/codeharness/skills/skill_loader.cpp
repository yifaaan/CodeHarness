#include "codeharness/skills/skill_loader.h"

#include <glob/glob.h>
#include <nonstd/expected.hpp>
#include <yaml-cpp/yaml.h>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

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

// yaml frontmatter 与 markdown 正文分开,
struct ParsedSkillMarkdown
{
    YAML::Node frontmatter;     // null Node:无 frontmatter 或解析失败
    std::string_view body;      // 跟在 frontmatter 之后的 markdown 正文
};

// 把 SKILL.md 内容切成 "yaml 头部" 和 "markdown 正文" 两段。
// 约定:以首行 `---` 开头、且在文件中能找到第二个 `---` 闭合行时,中间是 YAML;
// 否则把整篇内容当作正文(没有 frontmatter)。
auto parse_frontmatter(std::string_view content) -> ParsedSkillMarkdown
{
    // 第一行必须是 "---",否则视为无 frontmatter
    auto [first_line, offset] = next_line(content, 0);
    if (trim(first_line) != "---")
    {
        return ParsedSkillMarkdown{.body = content};
    }

    // 记录 yaml 区段的起点
    const auto frontmatter_start = offset;

    // 逐行扫描,直到遇到匹配的 "---" 闭合
    while (offset < content.size())
    {
        const auto line_start = offset;
        auto [line, next_offset] = next_line(content, offset);
        offset = next_offset;

        if (trim(line) == "---")
        {
            // 切片得到 yaml 原文
            const auto raw_yaml = content.substr(frontmatter_start, line_start - frontmatter_start);
            YAML::Node metadata;
            try
            {
                metadata = YAML::Load(std::string{raw_yaml});
            }
            catch (const YAML::Exception& e)
            {
                spdlog::warn("failed to parse skill frontmatter yaml: {}", e.what());
            }

            return ParsedSkillMarkdown{
                .frontmatter = std::move(metadata),
                .body = content.substr(offset),  // 闭合 "---" 之后剩下的就是正文
            };
        }
    }

    // 没有 frontmatter,整篇视为正文
    return ParsedSkillMarkdown{.body = content};
}

// 当 frontmatter 没给 name/description 时,从 markdown 正文里尽力提取:
//   - 第一个 "# 标题" 用作 name(仅在 name 仍是 default_name 时才会被覆盖,
//     避免误改用户在 frontmatter 里写过的 name);
//   - 第一个非空、非标题行截取最多 200 字符当作 description;
//   - 实在什么都没有就回退到 "Skill: <name>"。
auto description_from_body(std::string_view body, std::string& name, std::string_view default_name) -> std::string
{
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto [line, next_offset] = next_line(body, offset);
        offset = next_offset;
        const auto stripped = trim(line);

        // "# ..." 形式的标题,提取其内容作为 name
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
            continue;  // 标题行不能当作 description
        }

        // 第一个有内容的非标题行,截前 200 字符作为描述
        if (!stripped.empty() && !stripped.starts_with('#'))
        {
            return std::string{stripped.substr(0, 200)};
        }
    }

    // 全文只有标题或纯空:用 name 拼一个保底描述
    return "Skill: " + name;
}

// 检查路径里是否含 ".." 段(防止路径穿越攻击)
auto has_parent_reference(const std::filesystem::path& path) -> bool
{
    return std::ranges::any_of(path, [](const auto& part) { return part == ".."; });
}

// 判定一个 project 相对 skill 目录配置是否可接受:
//   - 非空
//   - 相对路径
//   - 不带根名(Windows 上 "C:" 这种)
//   - 不含 ".."
// 这些限制由配置层兜底,防止恶意或手误的配置把扫描范围带出工作目录。
auto is_safe_project_skill_dir(const std::filesystem::path& path) -> bool
{
    return !path.empty() && !path.is_absolute() && !path.has_root_name() && !has_parent_reference(path);
}

// 从 start 出发向上逐层找含 .git 的目录,找到则返回该目录;走到文件系统根都
// 没找到则返回 nullopt。用于限制 project skill 扫描范围(最多到 git 仓库根)。
// 这里用 std::filesystem::exists(error) 而非异常版,避免权限/IO 错误中断扫描。
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
        // 走到根目录仍没有 .git:parent 变空或自指,停止
        if (parent.empty() || parent == current)
        {
            return std::nullopt;
        }

        current = parent;
    }
}

// 取当前用户 home 目录(跨平台)。Windows 走 USERPROFILE,其它平台走 HOME。
// 环境变量未设置或为空字符串时返回 nullopt,调用方据此决定是否放弃加载 user skills。
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

// 默认的 "user 全局" skill 目录,优先级顺序:本项目 > 兼容 claude > 通用 agents。
// 顺序即加载顺序,前面的同名 skill 会覆盖后面的(由 SkillRegistry 的注册顺序决定)。
// 没有 home 时返回空 vector,后续 load_skills_from_dirs 会自然跳过。
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

// 解析单个 SKILL.md 的纯字符串内容(不含文件 I/O),生成 SkillDefinition。
// 字段来源优先级:
//   - name:        frontmatter "name" > 传入的 default_name > 正文里第一个 # 标题
//   - description: frontmatter "description" > 正文里第一个非空非标题行(<=200字)
// 其它字段(aliases / user-invocable / disable-model-invocation / model / argument-hint)
// 全部走 yaml_get_* helper,缺省/类型错误时回落到合理默认。
auto parse_skill_markdown(std::string_view default_name, std::string content, std::string source) -> SkillDefinition
{
    const auto parsed = parse_frontmatter(content);
    // 名字优先用 frontmatter;没有就用调用方给的默认值(通常是目录名)
    auto name = yaml_get_string(parsed.frontmatter, "name").value_or(std::string{default_name});
    auto description = yaml_get_string(parsed.frontmatter, "description").value_or(std::string{});

    // 描述为空时尝试从正文提取;提取过程中可能顺便覆盖 name
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

// 加载磁盘上单个 SKILL.md:读文件 -> 解析 -> 补充 path/base_dir/command_name 元数据。
// default_name 取父目录(也就是 skill 目录)的名字,这是注册命令时的 fallback 名字。
// 额外字段:
//   - path:        文件绝对/规范化路径
//   - base_dir:    skill 所在目录(供后续加载资源/子文件用)
//   - command_name: 默认斜杠命令名,等于目录名
//   - display_name: 当前置显示用的"漂亮"名,仅在 frontmatter name 与目录名不同时填入
auto load_skill_file(const std::filesystem::path& path, std::string source) -> Result<SkillDefinition>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    // skill 目录名作为 default name
    auto skill = parse_skill_markdown(path.parent_path().filename().string(), std::move(*content), std::move(source));
    skill.path = path;
    skill.base_dir = path.parent_path();
    skill.command_name = path.parent_path().filename().string();

    // 只有 frontmatter 里改了 name,才需要单独记录 display_name(让 UI 可以显示原始目录名)
    if (skill.name != *skill.command_name)
    {
        skill.display_name = skill.name;
    }

    return skill;
}

// 在一组目录里扫描 SKILL.md 并加载。约定:每个直接子目录算一个 skill,
// SKILL.md 位于 "<root>/<skill_name>/SKILL.md"。
// 关键行为:
//   - 目录不存在/不是目录:静默跳过(允许部分 user/project 目录缺失)
//   - 路径去重:用 canonical 路径放进 set,避免硬链接/符号链接导致同一 skill
//     被注册多次
//   - 排序:同一目录内按路径排序,保证加载顺序稳定 -> 注册表 key 冲突时
//     表现可重现
//   - 失败冒泡:glob 抛异常或 canonical 失败 -> 直接返回 Io 错误,不要静默吞掉
auto load_skills_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> Result<std::vector<SkillDefinition>>
{
    std::vector<SkillDefinition> skills;
    std::set<std::filesystem::path> seen;  // 已加载过的 canonical 路径,用于去重

    for (const auto& root : directories)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            continue;  // 单个目录缺失不算错
        }

        // glob 模式:例如 ~/.codeharness/skills/*/SKILL.md
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

        std::ranges::sort(matches);  // 稳定加载顺序

        for (const auto& candidate : matches)
        {
            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            if (error)
            {
                return fail<std::vector<SkillDefinition>>(
                    ErrorKind::Io,
                    fmt::format("failed to resolve skill path {}: {}", candidate.string(), error.message()));
            }

            // emplace 返回 false 表示之前已经见过,跳过
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


// 从 cwd 一路向上,收集到 git_root 为止的每一层 "skills 目录"。
// 举例:cwd = /repo/sub/mod,git_root = /repo,且配置了 [".codeharness/skills"]
//   -> 循环结束后 levels  = [cwd, parent, git_root]   (子→父)
//   -> reverse 之后 levels = [git_root, parent, cwd]  (父→子)
//   -> roots 里 git_root 的 skill 在前,cwd 的 skill 在最后
// 后续 register_skill 用 unordered_map::operator= 直接覆盖,
// 后注册者胜出 —— 即 cwd 的同名 skill 会覆盖 git_root 的同名 skill,
// 表现为"内层覆盖外层",与 Claude 的搜索行为一致。
// 安全性:相对目录必须通过 is_safe_project_skill_dir 校验,避免恶意配置越界。
// 范围限制:在 git_root 处停下,不递归到文件系统根,避免无意义的全盘扫描。
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

    // 1) 从 cwd 走到 git_root(若存在),把每层目录都记录下来
    std::vector<std::filesystem::path> levels;
    auto current = start;
    const auto git_root = find_git_root(start);

    while (true)
    {
        levels.push_back(current);
        // 找到 git_root 就到此为止;若根本没在 git 仓库里则一直走到 / 停下
        if (!git_root || current == *git_root)
        {
            break;
        }

        current = current.parent_path();
    }

    // 2) 反转 levels:遍历顺序从 git_root(外)到 cwd(内),
    //    使内层 skill 后注册,经由 register_skill 的覆盖语义实现
    //    "内层 skill 覆盖外层同名 skill"。
    std::ranges::reverse(levels);

    // 3) 对每一层应用每一种相对目录配置,生成候选 skill 根目录
    std::vector<std::filesystem::path> roots;
    std::set<std::filesystem::path> seen;

    for (const auto& level : levels)
    {
        for (const auto& relative_dir : relative_dirs)
        {
            if (!is_safe_project_skill_dir(relative_dir))
            {
                continue;  // 非法路径配置,跳过
            }

            const auto candidate = level / relative_dir;
            if (!std::filesystem::is_directory(candidate, error))
            {
                continue;  // 该层没这个 skill 目录,很正常
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

// 顶层组合器:按 "bundled → user → extra → project" 的顺序把所有来源的 skill
// 注册到同一个 SkillRegistry,后注册者同名时覆盖前注册者。
// 各来源说明:
//   - bundled_skills:        编译期/打包期内
//   - user_skill_dirs:       显式指定的 user 目录
//   - load_default_user_skills=true 时,会在 user_skill_dirs 前面插入默认 user 目录             
//   - extra_skill_dirs:      补充目录 CLI/配置追加,用 "user" 标签
//   - allow_project_skills:  是否扫描 cwd 到 git_root 的 project skill 目录
// 来源标签会被写进 SkillDefinition.source,用于 UI 区分 skill 的来源。
auto load_skill_registry(const std::filesystem::path& cwd, SkillLoadOptions options) -> Result<SkillRegistry>
{
    SkillRegistry registry;

    // 1) bundled skills(内存传入,直接注册)
    for (auto& skill : options.bundled_skills)
    {
        registry.register_skill(std::move(skill));
    }

    // 2) user skills:把默认 user 目录插到最前(若开启),再追加用户传入的目录
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

    // 3) extra skill 目录(CLI/配置追加,语义上仍属 user 范围)
    auto extra_skills = load_skills_from_dirs(options.extra_skill_dirs, "user");
    if (!extra_skills)
    {
        return nonstd::make_unexpected(extra_skills.error());
    }

    for (auto& skill : *extra_skills)
    {
        registry.register_skill(std::move(skill));
    }

    // 4) project skills:从 cwd 向上扫到 git_root,可由选项关闭
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

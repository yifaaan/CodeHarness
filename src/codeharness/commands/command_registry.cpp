#include "codeharness/commands/command_registry.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "codeharness/core/strings.h"

namespace codeharness
{

namespace
{

// 去掉command首部的空白和可选的 '/':
//   "   /skills"    -> "skills"
//   "/skills"       -> "skills"
//   "skills"        -> "skills"
auto strip_leading_slash(std::string_view input) -> std::string_view
{
    input = trim(input);
    if (!input.empty() && input.front() == '/')
    {
        input.remove_prefix(1);
    }

    return input;
}

// 把 "name arg1 arg2" 切成 (name, "arg1 arg2")。
auto split_command_line(std::string_view input) -> std::pair<std::string_view, std::string>
{
    input = strip_leading_slash(input);
    const auto separator = input.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos)
    {
        // 只有命令名,没有 args
        return {input, {}};
    }

    auto args = std::string{trim(input.substr(separator + 1))};
    return {input.substr(0, separator), std::move(args)};
}

// 列出 skill 时优先用 command_name(用户实际键入的名字),
auto format_skill_name(const SkillDefinition& skill) -> std::string
{
    return skill.command_name.value_or(skill.name);
}

// 把当前 SkillRegistry 渲染成 /skills 命令的多行文本输出。
// 输出格式:
//   Available skills:
//   - <command_name> [<source>]: <description>
//   - ...
auto format_skills_list(const SkillRegistry& skills) -> std::string
{
    auto listed = skills.list();

    std::ostringstream output;
    output << "Available skills:\n";

    for (const auto& skill : listed)
    {
        output << "- " << format_skill_name(skill) << " [" << skill.source << "]: " << skill.description << '\n';
    }

    return output.str();
}

auto replace_all(std::string input, std::string_view needle, std::string_view replacement) -> std::string
{
    if (needle.empty())
    {
        return input;
    }

    std::size_t position = 0;
    while ((position = input.find(needle, position)) != std::string::npos)
    {
        input.replace(position, needle.size(), replacement);
        position += replacement.size();
    }

    return input;
}

// 判断原始 skill 内容是否主动消费了参数。
// 注意检查的是替换前的 content:替换后再看会永远找不到占位符,
// 从而错误地追加一段重复的 "Arguments: ..."。
auto has_argument_placeholder(std::string_view content) -> bool
{
    return content.find("${ARGUMENTS}") != std::string_view::npos ||
           content.find("$ARGUMENTS") != std::string_view::npos;
}

// command-name/frontmatter 优先,否则用目录/skill name 作为 /<name>。
auto skill_command_name(const SkillDefinition& skill) -> std::string
{
    return skill.command_name.value_or(skill.name);
}

auto is_valid_skill_command_name(std::string_view name) -> bool
{
    return !name.empty() && std::ranges::none_of(name, [](unsigned char character) {
        return std::isspace(character) != 0;
    });
}

// 把一次用户 slash 调用转成真正发给模型的 prompt。
auto render_skill_command_prompt(const SkillDefinition& skill, std::string_view args) -> std::string
{
    auto prompt = skill.content;
    const auto raw_args = std::string{trim(args)};

    if (skill.base_dir)
    {
        const auto base_dir = skill.base_dir->string();
        prompt = "Base directory for this skill: " + base_dir + "\n\n" + prompt;
        prompt = replace_all(std::move(prompt), "${CLAUDE_SKILL_DIR}", base_dir);
    }

    prompt = replace_all(std::move(prompt), "${ARGUMENTS}", raw_args);
    prompt = replace_all(std::move(prompt), "$ARGUMENTS", raw_args);

    if (!raw_args.empty() && !has_argument_placeholder(skill.content))
    {
        prompt += "\n\nArguments: " + raw_args;
    }

    return prompt;
}

auto make_skill_slash_command(SkillDefinition skill) -> SlashCommand
{
    auto name = skill_command_name(skill);
    auto description = "Invoke the " + name + " skill.";

    return SlashCommand{
        .name = std::move(name),
        .description = std::move(description),
        .handler = [skill = std::move(skill)](std::string_view args) -> Result<CommandResult> {
            return CommandResult{
                .submit_prompt = render_skill_command_prompt(skill, args),
                .submit_model = skill.model,
            };
        },
    };
}

} // namespace

auto CommandRegistry::register_command(SlashCommand command) -> void
{
    auto stored = std::make_shared<SlashCommand>(std::move(command));

    const auto register_key = [&](const std::string& key) {
        if (!key.empty())
        {
            by_key_[key] = stored;
        }
    };

    register_key(stored->name);

    for (const auto& alias : stored->aliases)
    {
        register_key(alias);
    }
}

auto CommandRegistry::lookup(std::string_view input) const -> CommandLookup
{
    const auto [name, args] = split_command_line(input);
    const auto it = by_key_.find(std::string{name});
    if (it == by_key_.end())
    {
        return CommandLookup{.args = args};
    }

    return CommandLookup{
        .command = it->second.get(),
        .args = args,
    };
}

auto CommandRegistry::list() const -> std::vector<SlashCommand>
{
    std::vector<SlashCommand> commands;
    std::unordered_set<const SlashCommand*> seen;

    for (const auto& [_, command] : by_key_)
    {
        if (seen.emplace(command.get()).second)
        {
            commands.push_back(*command);
        }
    }

    std::ranges::sort(commands, [](const auto& left, const auto& right) { return left.name < right.name; });
    return commands;
}

// 内置命令注册点。新增内置命令只在这里加一个 register_command 调用;
auto build_builtin_command_registry(const SkillRegistry& skills) -> CommandRegistry
{
    CommandRegistry registry;
    registry.register_command(
        SlashCommand{
            .name = "skills",
            .description = "List loaded skills.",
            .handler = [&skills](std::string_view) -> Result<CommandResult> {
                return CommandResult{.message = format_skills_list(skills)};
            },
        });

    for (auto skill : skills.list())
    {
        if (!skill.user_invocable)
        {
            continue;
        }

        const auto command_name = skill_command_name(skill);
        if (!is_valid_skill_command_name(command_name))
        {
            continue;
        }

        if (registry.lookup(std::string{"/"} + command_name).command != nullptr)
        {
            continue;
        }

        registry.register_command(make_skill_slash_command(std::move(skill)));
    }

    return registry;
}

auto execute_slash_command(const CommandRegistry& registry, std::string_view input) -> Result<CommandResult>
{
    auto lookup = registry.lookup(input);
    if (lookup.command == nullptr)
    {
        const auto [name, _] = split_command_line(input);
        return fail<CommandResult>(ErrorKind::InvalidArgument, "unknown command: /" + std::string{name});
    }

    return lookup.command->handler(lookup.args);
}

} // namespace codeharness

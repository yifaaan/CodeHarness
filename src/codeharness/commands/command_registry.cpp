#include "codeharness/commands/command_registry.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "codeharness/core/strings.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/plugins/plugin_loader.h"

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

// command-name/frontmatter 优先,否则用目录/skill name 作为 /<name>。
auto skill_command_name(const SkillDefinition& skill) -> std::string
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
        output << "- " << skill_command_name(skill) << " [" << skill.source << "]: " << skill.description << '\n';
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

auto apply_argument_placeholders(std::string prompt, std::string_view args, std::string_view placeholder_source)
    -> std::string
{
    const auto raw_args = std::string{trim(args)};
    prompt = replace_all(std::move(prompt), "${ARGUMENTS}", raw_args);
    prompt = replace_all(std::move(prompt), "$ARGUMENTS", raw_args);

    if (!raw_args.empty() && !has_argument_placeholder(placeholder_source))
    {
        prompt += "\n\nArguments: " + raw_args;
    }

    return prompt;
}

auto is_valid_skill_command_name(std::string_view name) -> bool
{
    return !name.empty() &&
           std::ranges::none_of(name, [](unsigned char character) { return std::isspace(character) != 0; });
}

// 把一次用户 slash 调用转成真正发给模型的 prompt。
auto render_skill_command_prompt(const SkillDefinition& skill, std::string_view args) -> std::string
{
    auto prompt = skill.content;

    if (skill.base_dir)
    {
        const auto base_dir = skill.base_dir->string();
        prompt = "Base directory for this skill: " + base_dir + "\n\n" + prompt;
        prompt = replace_all(std::move(prompt), "${CLAUDE_SKILL_DIR}", base_dir);
    }

    return apply_argument_placeholders(std::move(prompt), args, skill.content);
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

auto split_subcommand(std::string_view args) -> std::pair<std::string_view, std::string>
{
    args = trim(args);
    if (args.empty())
    {
        return {"list", {}};
    }

    const auto separator = args.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos)
    {
        return {args, {}};
    }

    return {args.substr(0, separator), std::string{trim(args.substr(separator + 1))}};
}

auto format_memory_list(const std::vector<memory::MemoryHeader>& memories) -> std::string
{
    if (memories.empty())
    {
        return "No memories.\n";
    }

    std::ostringstream output;
    output << "Memories:\n";
    for (const auto& memory : memories)
    {
        output << "- " << memory.title << " [" << memory.metadata.id << "] " << memory.relative_path.string();
        if (!memory.description.empty())
        {
            output << ": " << memory.description;
        }
        output << '\n';
    }

    return output.str();
}

auto format_memory_search_results(const std::vector<memory::MemoryEntry>& memories) -> std::string
{
    if (memories.empty())
    {
        return "No matching memories.\n";
    }

    std::ostringstream output;
    output << "Matching memories:\n";
    for (const auto& memory : memories)
    {
        output << "- " << memory.header.title << " [" << memory.header.metadata.id << "]";
        if (!memory.header.description.empty())
        {
            output << ": " << memory.header.description;
        }
        output << '\n';
    }

    return output.str();
}

auto parse_add_memory_request(std::string_view args) -> Result<memory::AddMemoryRequest>
{
    const auto separator = args.find("::");
    if (separator == std::string_view::npos)
    {
        return fail<memory::AddMemoryRequest>(ErrorKind::InvalidArgument, "usage: /memory add TITLE :: BODY");
    }

    auto title = std::string{trim(args.substr(0, separator))};
    auto body = std::string{trim(args.substr(separator + 2))};
    if (title.empty() || body.empty())
    {
        return fail<memory::AddMemoryRequest>(ErrorKind::InvalidArgument, "usage: /memory add TITLE :: BODY");
    }

    return memory::AddMemoryRequest{
        .title = std::move(title),
        .body = std::move(body),
    };
}

auto execute_memory_command(memory::MemoryStore& store, std::string_view args) -> Result<CommandResult>
{
    const auto [subcommand, rest] = split_subcommand(args);

    if (subcommand == "list")
    {
        auto memories = store.scan();
        if (!memories)
        {
            return nonstd::make_unexpected(memories.error());
        }

        return CommandResult{.message = format_memory_list(*memories)};
    }

    if (subcommand == "add")
    {
        auto request = parse_add_memory_request(rest);
        if (!request)
        {
            return nonstd::make_unexpected(request.error());
        }

        auto memory = store.add(*request);
        if (!memory)
        {
            return nonstd::make_unexpected(memory.error());
        }

        return CommandResult{
            .message = "Added memory: " + memory->title + " (" + memory->relative_path.string() + ")\n",
        };
    }

    if (subcommand == "search")
    {
        const auto query = std::string{trim(rest)};
        if (query.empty())
        {
            return fail<CommandResult>(ErrorKind::InvalidArgument, "usage: /memory search QUERY");
        }

        auto memories = store.search(query);
        if (!memories)
        {
            return nonstd::make_unexpected(memories.error());
        }

        return CommandResult{.message = format_memory_search_results(*memories)};
    }

    if (subcommand == "remove")
    {
        const auto target = std::string{trim(rest)};
        if (target.empty())
        {
            return fail<CommandResult>(ErrorKind::InvalidArgument, "usage: /memory remove NAME_OR_ID");
        }

        auto removed = store.soft_remove(target);
        if (!removed)
        {
            return nonstd::make_unexpected(removed.error());
        }

        if (*removed)
        {
            return CommandResult{.message = "Removed memory: " + target + "\n"};
        }

        return CommandResult{.message = "No memory found: " + target + "\n"};
    }

    return fail<CommandResult>(ErrorKind::InvalidArgument, "unknown memory command: " + std::string{subcommand});
}

auto register_memory_command(CommandRegistry& registry, memory::MemoryStore* memory_store) -> void
{
    if (memory_store == nullptr)
    {
        return;
    }

    registry.register_command(
        SlashCommand{
            .name = "memory",
            .description = "Manage project memory.",
            .handler = [memory_store](std::string_view args) -> Result<CommandResult> {
                return execute_memory_command(*memory_store, args);
            },
        });
}

auto format_plugin_list(std::span<const LoadedPlugin> plugins) -> std::string
{
    if (plugins.empty())
    {
        return "No plugins.\n";
    }

    std::ostringstream output;
    output << "Plugins:\n";
    for (const auto& plugin : plugins)
    {
        output << "- " << plugin.manifest.name << " [" << (plugin.enabled ? "enabled" : "disabled") << "] "
               << plugin.manifest.version;
        if (!plugin.manifest.description.empty())
        {
            output << ": " << plugin.manifest.description;
        }
        output << " (skills: " << plugin.skills.size() << ", commands: " << plugin.commands.size() << ", mcp: "
               << plugin.mcp_servers.size() << ")";
        output << '\n';
    }

    return output.str();
}

auto execute_plugin_command(std::string_view plugin_list, std::string_view args) -> Result<CommandResult>
{
    const auto [subcommand, rest] = split_subcommand(args);
    std::ignore = rest;

    if (subcommand == "list")
    {
        return CommandResult{.message = std::string{plugin_list}};
    }

    return fail<CommandResult>(ErrorKind::InvalidArgument, "unknown plugin command: " + std::string{subcommand});
}

auto register_plugin_command(CommandRegistry& registry, std::span<const LoadedPlugin> plugins) -> void
{
    auto plugin_list = format_plugin_list(plugins);
    registry.register_command(
        SlashCommand{
            .name = "plugin",
            .description = "List loaded plugins.",
            .handler = [plugin_list = std::move(plugin_list)](std::string_view args) -> Result<CommandResult> {
                return execute_plugin_command(plugin_list, args);
            },
        });
}

auto render_plugin_command_prompt(const PluginCommandDefinition& command, std::string_view args) -> std::string
{
    return apply_argument_placeholders(command.content, args, command.content);
}

auto make_plugin_slash_command(PluginCommandDefinition command) -> SlashCommand
{
    auto description = command.description;
    if (description.empty())
    {
        description = "Invoke the " + command.command_name + " plugin command.";
    }

    return SlashCommand{
        .name = command.command_name,
        .description = std::move(description),
        .handler = [command = std::move(command)](std::string_view args) -> Result<CommandResult> {
            auto prompt = render_plugin_command_prompt(command, args);
            if (command.disable_model_invocation)
            {
                return CommandResult{.message = std::move(prompt)};
            }

            return CommandResult{
                .submit_prompt = std::move(prompt),
                .submit_model = command.model,
            };
        },
    };
}

auto register_plugin_slash_commands(CommandRegistry& registry, std::span<const LoadedPlugin> plugins) -> void
{
    for (const auto& plugin : plugins)
    {
        if (!plugin.enabled)
        {
            continue;
        }

        for (auto command : plugin.commands)
        {
            if (!is_valid_skill_command_name(command.command_name))
            {
                continue;
            }

            if (registry.lookup(std::string{"/"} + command.command_name).command != nullptr)
            {
                continue;
            }

            registry.register_command(make_plugin_slash_command(std::move(command)));
        }
    }
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
auto build_builtin_command_registry(const SkillRegistry& skills, BuiltinCommandRegistryOptions options)
    -> CommandRegistry
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
    register_memory_command(registry, options.memory_store);
    register_plugin_command(registry, options.plugins);
    register_plugin_slash_commands(registry, options.plugins);

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

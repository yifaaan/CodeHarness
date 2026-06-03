#include "codeharness/tasks/task_tools.h"

#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "codeharness/core/json_parse.h"

namespace codeharness::tasks
{

namespace
{

struct TaskCreateInput
{
    std::string type;
    std::string description;
    std::optional<std::string> command;
    std::optional<std::string> prompt;
    std::optional<std::string> model;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
};

struct AgentInput
{
    std::string description;
    std::string prompt;
    std::string mode;
    std::optional<std::string> subagent_type;
    std::optional<std::string> model;
    std::optional<std::string> command;
    std::optional<std::string> team;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
};

auto parse_task_id(const nlohmann::json& input, std::string_view tool_name) -> Result<std::string>
{
    return read_json_field<std::string>(input, "task_id", tool_name);
}

auto task_response_json(const TaskRecord& record) -> nlohmann::json
{
    const nlohmann::json json = record;
    return json;
}

auto join_argv(const std::vector<std::string>& argv) -> std::string
{
    std::string command;
    for (const auto& arg : argv)
    {
        if (!command.empty())
        {
            command += ' ';
        }
        command += arg;
    }

    return command;
}

auto argv_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    PermissionTarget target;

    auto argv = read_json_field<std::vector<std::string>, JsonFieldMode::optional_if_valid>(input, "argv");
    if (!argv || !*argv || (*argv)->empty())
    {
        return target;
    }

    target.command = join_argv(**argv);
    return target;
}

auto local_agent_default_permission_target() -> PermissionTarget
{
    PermissionTarget target;
    target.command = "codeharness";
    return target;
}

auto input_type(const nlohmann::json& input, std::string_view default_type) -> std::string
{
    auto type = read_json_field<std::string, JsonFieldMode::optional_if_valid>(input, "type");
    if (type && *type)
    {
        return **type;
    }

    auto mode = read_json_field<std::string, JsonFieldMode::optional_if_valid>(input, "mode");
    if (mode && *mode)
    {
        return **mode;
    }

    return std::string{default_type};
}

auto command_or_argv_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    auto target = command_permission_target(input, "command");
    if (target.command)
    {
        return target;
    }

    return argv_permission_target(input);
}

auto create_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    auto target = command_or_argv_permission_target(input);
    if (target.command)
    {
        return target;
    }

    if (input_type(input, "local_bash") == "local_agent")
    {
        return local_agent_default_permission_target();
    }

    return target;
}

auto agent_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    auto target = command_or_argv_permission_target(input);
    if (target.command)
    {
        return target;
    }

    return local_agent_default_permission_target();
}

auto agent_id_for(const AgentInput& input) -> std::string
{
    auto agent_id = input.subagent_type.value_or("agent");
    agent_id += '@';
    agent_id += input.team.value_or("default");
    return agent_id;
}

auto agent_task_response(const ToolRequest& request, const AgentInput& input, const TaskRecord& record) -> ToolResponse
{
    const auto agent_id = agent_id_for(input);
    auto output = nlohmann::json{
        {"agent_id", agent_id},
        {"task_id", record.id},
        {"backend_type", "subprocess"},
        {"description", input.description},
        {"task", task_response_json(record)},
    };

    return ToolResponse{
        .tool_use_id = request.id,
        .content = output.dump(2),
    };
}

auto task_response(const ToolRequest& request, const TaskRecord& record) -> ToolResponse
{
    return ToolResponse{
        .tool_use_id = request.id,
        .content = task_response_json(record).dump(2),
    };
}

auto missing_task_response(const ToolRequest& request, std::string_view id) -> ToolResponse
{
    return ToolResponse{
        .tool_use_id = request.id,
        .content = "No task found with ID: " + std::string{id},
        .is_error = true,
    };
}

auto parse_create_input(const nlohmann::json& input) -> Result<TaskCreateInput>
{
    auto type = read_json_field<std::string, JsonFieldMode::optional_with_default>(input, "type", "task_create", std::string{"local_bash"});
    if (!type)
    {
        return nonstd::make_unexpected(type.error());
    }

    auto description = read_json_field<std::string>(input, "description", "task_create");
    if (!description)
    {
        return nonstd::make_unexpected(description.error());
    }

    auto command = read_nullable_optional_json_field<std::string>(input, "command", "task_create");
    if (!command)
    {
        return nonstd::make_unexpected(command.error());
    }

    auto prompt = read_nullable_optional_json_field<std::string>(input, "prompt", "task_create");
    if (!prompt)
    {
        return nonstd::make_unexpected(prompt.error());
    }

    auto model = read_nullable_optional_json_field<std::string>(input, "model", "task_create");
    if (!model)
    {
        return nonstd::make_unexpected(model.error());
    }

    auto argv = read_nullable_json_field<std::vector<std::string>>(input, "argv", "task_create");
    if (!argv)
    {
        return nonstd::make_unexpected(argv.error());
    }

    auto env = read_nullable_json_field<std::map<std::string, std::string>>(input, "env", "task_create");
    if (!env)
    {
        return nonstd::make_unexpected(env.error());
    }

    return TaskCreateInput{
        .type = std::move(*type),
        .description = std::move(*description),
        .command = std::move(*command),
        .prompt = std::move(*prompt),
        .model = std::move(*model),
        .argv = std::move(*argv),
        .env = std::move(*env),
    };
}

auto parse_agent_input(const nlohmann::json& input) -> Result<AgentInput>
{
    auto description = read_json_field<std::string>(input, "description", "agent");
    if (!description)
    {
        return nonstd::make_unexpected(description.error());
    }

    auto prompt = read_json_field<std::string>(input, "prompt", "agent");
    if (!prompt)
    {
        return nonstd::make_unexpected(prompt.error());
    }

    auto mode = read_json_field<std::string, JsonFieldMode::optional_with_default>(input, "mode", "agent", std::string{"local_agent"});
    if (!mode)
    {
        return nonstd::make_unexpected(mode.error());
    }

    auto subagent_type = read_nullable_optional_json_field<std::string>(input, "subagent_type", "agent");
    if (!subagent_type)
    {
        return nonstd::make_unexpected(subagent_type.error());
    }

    auto model = read_nullable_optional_json_field<std::string>(input, "model", "agent");
    if (!model)
    {
        return nonstd::make_unexpected(model.error());
    }

    auto command = read_nullable_optional_json_field<std::string>(input, "command", "agent");
    if (!command)
    {
        return nonstd::make_unexpected(command.error());
    }

    auto team = read_nullable_optional_json_field<std::string>(input, "team", "agent");
    if (!team)
    {
        return nonstd::make_unexpected(team.error());
    }

    auto argv = read_nullable_json_field<std::vector<std::string>>(input, "argv", "agent");
    if (!argv)
    {
        return nonstd::make_unexpected(argv.error());
    }

    auto env = read_nullable_json_field<std::map<std::string, std::string>>(input, "env", "agent");
    if (!env)
    {
        return nonstd::make_unexpected(env.error());
    }

    return AgentInput{
        .description = std::move(*description),
        .prompt = std::move(*prompt),
        .mode = std::move(*mode),
        .subagent_type = std::move(*subagent_type),
        .model = std::move(*model),
        .command = std::move(*command),
        .team = std::move(*team),
        .argv = std::move(*argv),
        .env = std::move(*env),
    };
}

auto shell_spec_from(const TaskCreateInput& input, const ToolContext& context) -> ShellTaskSpec
{
    return ShellTaskSpec{
        .description = input.description,
        .cwd = context.cwd,
        .command = input.command,
        .argv = input.argv,
        .env = input.env,
    };
}

auto agent_spec_from(const TaskCreateInput& input, const ToolContext& context) -> Result<AgentTaskSpec>
{
    if (!input.prompt)
    {
        return fail<AgentTaskSpec>(ErrorKind::InvalidArgument, "prompt is required for local_agent tasks");
    }

    return AgentTaskSpec{
        .description = input.description,
        .cwd = context.cwd,
        .prompt = *input.prompt,
        .command = input.command,
        .argv = input.argv,
        .env = input.env,
        .model = input.model,
    };
}

auto agent_spec_from(const AgentInput& input, const ToolContext& context) -> AgentTaskSpec
{
    auto metadata = std::map<std::string, std::string>{
        {"agent_id", agent_id_for(input)},
        {"backend_type", "subprocess"},
    };
    if (input.subagent_type)
    {
        metadata["subagent_type"] = *input.subagent_type;
    }
    if (input.team)
    {
        metadata["team"] = *input.team;
    }

    return AgentTaskSpec{
        .description = input.description,
        .cwd = context.cwd,
        .prompt = input.prompt,
        .command = input.command,
        .argv = input.argv,
        .env = input.env,
        .model = input.model,
        .metadata = std::move(metadata),
    };
}

auto unsupported_mode_response(const ToolRequest& request, std::string_view) -> ToolResponse
{
    return ToolResponse{
        .tool_use_id = request.id,
        .content = "Invalid mode. Use local_agent.",
        .is_error = true,
    };
}

auto create_local_agent_task(TaskManager& manager, const TaskCreateInput& input, const ToolContext& context) -> Result<TaskRecord>
{
    auto spec = agent_spec_from(input, context);
    if (!spec)
    {
        return nonstd::make_unexpected(spec.error());
    }

    return manager.create_agent_task(*spec);
}

auto create_agent_task(TaskManager& manager, const AgentInput& input, const ToolContext& context) -> Result<TaskRecord>
{
    return manager.create_agent_task(agent_spec_from(input, context));
}

} // namespace

TaskCreateTool::TaskCreateTool(TaskManager& manager) : manager_{manager}
{
}

auto TaskCreateTool::name() const -> std::string
{
    return "task_create";
}

auto TaskCreateTool::description() const -> std::string
{
    return "Create a background local shell or local agent task.";
}

auto TaskCreateTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return create_permission_target(request.parsed_input);
}

auto TaskCreateTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto input = parse_create_input(request.parsed_input);
    if (!input)
    {
        return nonstd::make_unexpected(input.error());
    }

    Result<TaskRecord> record = fail<TaskRecord>(ErrorKind::InvalidArgument, "unsupported task type: " + input->type);
    if (input->type == "local_bash")
    {
        record = manager_.create_shell_task(shell_spec_from(*input, context));
    }
    else if (input->type == "local_agent")
    {
        record = create_local_agent_task(manager_, *input, context);
    }

    if (!record)
    {
        return nonstd::make_unexpected(record.error());
    }

    return task_response(request, *record);
}

TaskListTool::TaskListTool(TaskManager& manager) : manager_{manager}
{
}

auto TaskListTool::name() const -> std::string
{
    return "task_list";
}

auto TaskListTool::description() const -> std::string
{
    return "List background tasks.";
}

auto TaskListTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto TaskListTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    std::optional<TaskStatus> filter;
    auto status = read_nullable_optional_json_field<std::string>(request.parsed_input, "status", "task_list");
    if (!status)
    {
        return nonstd::make_unexpected(status.error());
    }
    if (*status)
    {
        auto parsed = parse_task_status(**status);
        if (!parsed)
        {
            return nonstd::make_unexpected(parsed.error());
        }
        filter = *parsed;
    }

    auto tasks = manager_.list_tasks(filter);
    if (!tasks)
    {
        return nonstd::make_unexpected(tasks.error());
    }

    nlohmann::json output = nlohmann::json::array();
    for (const auto& task : *tasks)
    {
        output.push_back(task_response_json(task));
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = output.dump(2),
    };
}

TaskGetTool::TaskGetTool(TaskManager& manager) : manager_{manager}
{
}

auto TaskGetTool::name() const -> std::string
{
    return "task_get";
}

auto TaskGetTool::description() const -> std::string
{
    return "Get details for a background task.";
}

auto TaskGetTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto TaskGetTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    auto id = parse_task_id(request.parsed_input, "task_get");
    if (!id)
    {
        return nonstd::make_unexpected(id.error());
    }

    auto record = manager_.get_task(*id);
    if (!record)
    {
        return nonstd::make_unexpected(record.error());
    }
    if (!*record)
    {
        return missing_task_response(request, *id);
    }

    return task_response(request, **record);
}

TaskOutputTool::TaskOutputTool(TaskManager& manager) : manager_{manager}
{
}

auto TaskOutputTool::name() const -> std::string
{
    return "task_output";
}

auto TaskOutputTool::description() const -> std::string
{
    return "Read the output log for a background task.";
}

auto TaskOutputTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto TaskOutputTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    auto id = parse_task_id(request.parsed_input, "task_output");
    if (!id)
    {
        return nonstd::make_unexpected(id.error());
    }

    auto max_bytes = read_json_field<int, JsonFieldMode::optional_with_default>(request.parsed_input, "max_bytes", "task_output", 12000);
    if (!max_bytes)
    {
        return nonstd::make_unexpected(max_bytes.error());
    }
    if (*max_bytes < 1 || *max_bytes > 100000)
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, "task_output max_bytes out of range");
    }

    auto output = manager_.read_output_tail(*id, static_cast<std::size_t>(*max_bytes));
    if (!output)
    {
        return nonstd::make_unexpected(output.error());
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = output->empty() ? "(no output)" : std::move(*output),
    };
}

TaskStopTool::TaskStopTool(TaskManager& manager) : manager_{manager}
{
}

auto TaskStopTool::name() const -> std::string
{
    return "task_stop";
}

auto TaskStopTool::description() const -> std::string
{
    return "Stop a background task.";
}

auto TaskStopTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    auto id = parse_task_id(request.parsed_input, "task_stop");
    if (!id)
    {
        return nonstd::make_unexpected(id.error());
    }

    auto record = manager_.stop_task(*id);
    if (!record)
    {
        return nonstd::make_unexpected(record.error());
    }

    return task_response(request, *record);
}

AgentTool::AgentTool(TaskManager& manager) : manager_{manager}
{
}

auto AgentTool::name() const -> std::string
{
    return "agent";
}

auto AgentTool::description() const -> std::string
{
    return "Spawn a local background agent task.";
}

auto AgentTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return agent_permission_target(request.parsed_input);
}

auto AgentTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto input = parse_agent_input(request.parsed_input);
    if (!input)
    {
        return nonstd::make_unexpected(input.error());
    }

    if (input->mode != "local_agent")
    {
        return unsupported_mode_response(request, input->mode);
    }

    auto record = create_agent_task(manager_, *input, context);
    if (!record)
    {
        return nonstd::make_unexpected(record.error());
    }

    return agent_task_response(request, *input, *record);
}

auto register_task_tools(ToolRegistry& registry, TaskManager& manager) -> void
{
    registry.add(std::make_unique<TaskCreateTool>(manager));
    registry.add(std::make_unique<TaskListTool>(manager));
    registry.add(std::make_unique<TaskGetTool>(manager));
    registry.add(std::make_unique<TaskOutputTool>(manager));
    registry.add(std::make_unique<TaskStopTool>(manager));
    registry.add(std::make_unique<AgentTool>(manager));
}

} // namespace codeharness::tasks

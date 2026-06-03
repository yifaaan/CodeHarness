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

auto parse_task_id(const nlohmann::json& input, std::string_view tool_name) -> Result<std::string>
{
    return read_json_field<std::string>(input, "task_id", tool_name);
}

auto task_response_json(const TaskRecord& record) -> nlohmann::json
{
    const nlohmann::json json = record;
    return json;
}

auto argv_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    PermissionTarget target;

    auto argv = read_json_field<std::vector<std::string>, JsonFieldMode::optional_if_valid>(input, "argv");
    if (!argv || !*argv || (*argv)->empty())
    {
        return target;
    }

    std::string command;
    for (const auto& arg : **argv)
    {
        if (!command.empty())
        {
            command += ' ';
        }
        command += arg;
    }

    target.command = std::move(command);
    return target;
}

auto create_permission_target(const nlohmann::json& input) -> PermissionTarget
{
    auto target = command_permission_target(input, "command");
    if (target.command)
    {
        return target;
    }

    return argv_permission_target(input);
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

auto parse_create_input(const nlohmann::json& input, const ToolContext& context) -> Result<ShellTaskSpec>
{
    const auto type = read_json_field<std::string, JsonFieldMode::optional_with_default>(input, "type", "task_create", std::string{"local_bash"});
    if (!type)
    {
        return nonstd::make_unexpected(type.error());
    }
    if (*type != "local_bash")
    {
        return fail<ShellTaskSpec>(ErrorKind::InvalidArgument, "unsupported task type: " + *type);
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

    return ShellTaskSpec{
        .description = std::move(*description),
        .cwd = context.cwd,
        .command = std::move(*command),
        .argv = std::move(*argv),
        .env = std::move(*env),
    };
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
    return "Create a background local shell task.";
}

auto TaskCreateTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return create_permission_target(request.parsed_input);
}

auto TaskCreateTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto spec = parse_create_input(request.parsed_input, context);
    if (!spec)
    {
        return nonstd::make_unexpected(spec.error());
    }

    auto record = manager_.create_shell_task(*spec);
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

auto register_task_tools(ToolRegistry& registry, TaskManager& manager) -> void
{
    registry.add(std::make_unique<TaskCreateTool>(manager));
    registry.add(std::make_unique<TaskListTool>(manager));
    registry.add(std::make_unique<TaskGetTool>(manager));
    registry.add(std::make_unique<TaskOutputTool>(manager));
    registry.add(std::make_unique<TaskStopTool>(manager));
}

} // namespace codeharness::tasks

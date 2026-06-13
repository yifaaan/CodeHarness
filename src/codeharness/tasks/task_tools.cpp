#include "codeharness/tasks/task_tools.h"

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "codeharness/core/json_parse.h"

namespace codeharness::tasks {

namespace {

struct TaskCreateInput {
  std::string type;
  std::string description;
  std::optional<std::string> command;
  std::optional<std::string> prompt;
  std::optional<std::string> model;
  std::vector<std::string> argv;
  std::map<std::string, std::string> env;
};

struct AgentInput {
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

auto parse_task_id(const nlohmann::json& input, std::string_view tool_name) -> absl::StatusOr<std::string> {
  return ReadJsonField<std::string>(input, "task_id", tool_name);
}

auto task_response_json(const TaskRecord& record) -> nlohmann::json {
  const nlohmann::json json = record;
  return json;
}

auto join_argv(const std::vector<std::string>& argv) -> std::string {
  std::string command;
  for (const auto& arg : argv) {
    if (!command.empty()) {
      command += ' ';
    }
    command += arg;
  }

  return command;
}

auto argv_permission_target(const nlohmann::json& input) -> PermissionTarget {
  PermissionTarget target;

  auto argv = ReadJsonField<std::vector<std::string>, JsonFieldMode::kOptionalIfValid>(input, "argv");
  if (!argv || !*argv || (*argv)->empty()) {
    return target;
  }

  target.command = join_argv(**argv);
  return target;
}

auto local_agent_default_permission_target() -> PermissionTarget {
  PermissionTarget target;
  target.command = "codeharness";
  return target;
}

auto input_type(const nlohmann::json& input, std::string_view default_type) -> std::string {
  auto type = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(input, "type");
  if (type && *type) {
    return **type;
  }

  auto mode = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(input, "mode");
  if (mode && *mode) {
    return **mode;
  }

  return std::string{default_type};
}

auto command_or_argv_permission_target(const nlohmann::json& input) -> PermissionTarget {
  auto target = CommandPermissionTarget(input, "command");
  if (target.command) {
    return target;
  }

  return argv_permission_target(input);
}

auto create_permission_target(const nlohmann::json& input) -> PermissionTarget {
  auto target = command_or_argv_permission_target(input);
  if (target.command) {
    return target;
  }

  if (input_type(input, "local_bash") == "local_agent") {
    return local_agent_default_permission_target();
  }

  return target;
}

auto agent_permission_target(const nlohmann::json& input) -> PermissionTarget {
  auto target = command_or_argv_permission_target(input);
  if (target.command) {
    return target;
  }

  return local_agent_default_permission_target();
}

auto agent_id_for(const AgentInput& input) -> std::string {
  auto agent_id = input.subagent_type.value_or("agent");
  agent_id += '@';
  agent_id += input.team.value_or("default");
  return agent_id;
}

auto agent_spawn_request_from(const AgentInput& input, const ToolContext& context) -> AgentSpawnRequest {
  return AgentSpawnRequest{
      .description = input.description,
      .prompt = input.prompt,
      .mode = input.mode,
      .subagent_type = input.subagent_type,
      .model = input.model,
      .command = input.command,
      .team = input.team,
      .argv = input.argv,
      .env = input.env,
      .cwd = context.cwd,
  };
}

auto task_response(const ToolRequest& request, const TaskRecord& record) -> ToolResponse {
  return ToolResponse{
      .tool_use_id = request.id,
      .content = task_response_json(record).dump(2),
  };
}

auto missing_task_response(const ToolRequest& request, std::string_view id) -> ToolResponse {
  return ToolResponse{
      .tool_use_id = request.id,
      .content = "No task found with ID: " + std::string{id},
      .is_error = true,
  };
}

auto parse_create_input(const nlohmann::json& input) -> absl::StatusOr<TaskCreateInput> {
  auto type = ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(input, "type", "task_create",
                                                                              std::string{"local_bash"});
  if (!type) {
    return type.error();
  }

  auto description = ReadJsonField<std::string>(input, "description", "task_create");
  if (!description) {
    return description.error();
  }

  auto command = ReadNullableOptionalJsonField<std::string>(input, "command", "task_create");
  if (!command.ok()) {
    return command.status();
  }

  auto prompt = ReadNullableOptionalJsonField<std::string>(input, "prompt", "task_create");
  if (!prompt) {
    return prompt.error();
  }

  auto model = ReadNullableOptionalJsonField<std::string>(input, "model", "task_create");
  if (!model) {
    return model.error();
  }

  auto argv = ReadNullableJsonField<std::vector<std::string>>(input, "argv", "task_create");
  if (!argv) {
    return argv.error();
  }

  auto env = ReadNullableJsonField<std::map<std::string, std::string>>(input, "env", "task_create");
  if (!env) {
    return env.error();
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

auto parse_agent_input(const nlohmann::json& input) -> absl::StatusOr<AgentInput> {
  auto description = ReadJsonField<std::string>(input, "description", "agent");
  if (!description) {
    return description.error();
  }

  auto prompt = ReadJsonField<std::string>(input, "prompt", "agent");
  if (!prompt) {
    return prompt.error();
  }

  auto mode = ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(input, "mode", "agent",
                                                                              std::string{"local_agent"});
  if (!mode) {
    return mode.error();
  }

  auto subagent_type = ReadNullableOptionalJsonField<std::string>(input, "subagent_type", "agent");
  if (!subagent_type) {
    return subagent_type.error();
  }

  auto model = ReadNullableOptionalJsonField<std::string>(input, "model", "agent");
  if (!model) {
    return model.error();
  }

  auto command = ReadNullableOptionalJsonField<std::string>(input, "command", "agent");
  if (!command.ok()) {
    return command.status();
  }

  auto team = ReadNullableOptionalJsonField<std::string>(input, "team", "agent");
  if (!team.ok()) {
    return team.status();
  }

  auto argv = ReadNullableJsonField<std::vector<std::string>>(input, "argv", "agent");
  if (!argv) {
    return argv.error();
  }

  auto env = ReadNullableJsonField<std::map<std::string, std::string>>(input, "env", "agent");
  if (!env) {
    return env.error();
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

auto agent_spec_from(const AgentInput& input, const ToolContext& context) -> AgentTaskSpec {
  auto metadata = std::map<std::string, std::string>{
      {"agent_id", agent_id_for(input)},
      {"backend_type", "subprocess"},
  };
  if (input.subagent_type) {
    metadata["subagent_type"] = *input.subagent_type;
  }
  if (input.team) {
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

auto register_task_tools_impl(ToolRegistry& registry, TaskManager& manager, AgentSpawnHandler spawn_handler) -> void {
  registry.add(std::make_unique<TaskCreateTool>(manager));
  registry.add(std::make_unique<TaskListTool>(manager));
  registry.add(std::make_unique<TaskGetTool>(manager));
  registry.add(std::make_unique<TaskOutputTool>(manager));
  registry.add(std::make_unique<TaskStopTool>(manager));
  if (spawn_handler) {
    registry.add(std::make_unique<AgentTool>(manager, std::move(spawn_handler)));
    return;
  }

  registry.add(std::make_unique<AgentTool>(manager));
}

}  // namespace

auto to_json(nlohmann::json& output, const AgentSpawnRequest& request) -> void {
  output = nlohmann::json{
      {"description", request.description},
      {"prompt", request.prompt},
      {"mode", request.mode},
      {"subagent_type", OptionalToJson(request.subagent_type)},
      {"model", OptionalToJson(request.model)},
      {"command", OptionalToJson(request.command)},
      {"team", OptionalToJson(request.team)},
      {"argv", request.argv},
      {"env", request.env},
      {"cwd", request.cwd.string()},
  };
}

auto from_json(const nlohmann::json& input, AgentSpawnRequest& request) -> void {
  request = AgentSpawnRequest{
      .description = ExpectJsonField(ReadJsonField<std::string>(input, "description", "agent spawn request")),
      .prompt = ExpectJsonField(ReadJsonField<std::string>(input, "prompt", "agent spawn request")),
      .mode = ExpectJsonField(ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(
          input, "mode", "agent spawn request", std::string{"local_agent"})),
      .subagent_type =
          ExpectJsonField(ReadNullableOptionalJsonField<std::string>(input, "subagent_type", "agent spawn request")),
      .model = ExpectJsonField(ReadNullableOptionalJsonField<std::string>(input, "model", "agent spawn request")),
      .command = ExpectJsonField(ReadNullableOptionalJsonField<std::string>(input, "command", "agent spawn request")),
      .team = ExpectJsonField(ReadNullableOptionalJsonField<std::string>(input, "team", "agent spawn request")),
      .argv = ExpectJsonField(ReadNullableJsonField<std::vector<std::string>>(input, "argv", "agent spawn request")),
      .env = ExpectJsonField(
          ReadNullableJsonField<std::map<std::string, std::string>>(input, "env", "agent spawn request")),
      .cwd = std::filesystem::path{ExpectJsonField(ReadJsonField<std::string>(input, "cwd", "agent spawn request"))},
  };
}

auto to_json(nlohmann::json& output, const AgentSpawnResponse& response) -> void {
  output = nlohmann::json{
      {"agent_id", response.agent_id},       {"task_id", response.task_id}, {"backend_type", response.backend_type},
      {"description", response.description}, {"task", response.task},
  };
}

auto from_json(const nlohmann::json& input, AgentSpawnResponse& response) -> void {
  response = AgentSpawnResponse{
      .agent_id = ExpectJsonField(ReadJsonField<std::string>(input, "agent_id", "agent spawn response")),
      .task_id = ExpectJsonField(ReadJsonField<std::string>(input, "task_id", "agent spawn response")),
      .backend_type = ExpectJsonField(ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(
          input, "backend_type", "agent spawn response", std::string{"subprocess"})),
      .description = ExpectJsonField(ReadJsonField<std::string>(input, "description", "agent spawn response")),
      .task = input.at("task").get<TaskRecord>(),
  };
}

TaskCreateTool::TaskCreateTool(TaskManager& manager) : manager_{manager} {}

auto TaskCreateTool::name() const -> std::string { return "task_create"; }

auto TaskCreateTool::description() const -> std::string {
  return "Create a background local shell or local agent task.";
}

auto TaskCreateTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return create_permission_target(request.parsed_input);
}

auto TaskCreateTool::execute(const ToolRequest& request, const ToolContext& context) const
    -> absl::StatusOr<ToolResponse> {
  auto input = parse_create_input(request.parsed_input);
  if (!input.ok()) {
    return input.status();
  }

  absl::StatusOr<TaskRecord> record =
      absl::StatusOr<TaskRecord>(absl::InvalidArgumentError("unsupported task type: " + input->type));
  if (input->type == "local_bash") {
    record = manager_.create_shell_task(ShellTaskSpec{
        .description = input->description,
        .cwd = context.cwd,
        .command = input->command,
        .argv = input->argv,
        .env = input->env,
    });
  } else if (input->type == "local_agent") {
    if (!input->prompt) {
      return absl::StatusOr<ToolResponse>(absl::InvalidArgumentError("prompt is required for local_agent tasks"));
    }
    record = manager_.create_agent_task(AgentTaskSpec{
        .description = input->description,
        .cwd = context.cwd,
        .prompt = *input->prompt,
        .command = input->command,
        .argv = input->argv,
        .env = input->env,
        .model = input->model,
    });
  }

  if (!record.ok()) {
    return record.status();
  }

  return task_response(request, *record);
}

TaskListTool::TaskListTool(TaskManager& manager) : manager_{manager} {}

auto TaskListTool::name() const -> std::string { return "task_list"; }

auto TaskListTool::description() const -> std::string { return "List background tasks."; }

auto TaskListTool::is_read_only() const noexcept -> bool { return true; }

auto TaskListTool::execute(const ToolRequest& request, const ToolContext&) const -> absl::StatusOr<ToolResponse> {
  std::optional<TaskStatus> filter;
  auto status = ReadNullableOptionalJsonField<std::string>(request.parsed_input, "status", "task_list");
  if (!status) {
    return status.error();
  }
  if (*status) {
    auto parsed = parse_task_status(**status);
    if (!parsed.ok()) {
      return parsed.status();
    }
    filter = *parsed;
  }

  auto tasks = manager_.list_tasks(filter);
  if (!tasks) {
    return tasks.error();
  }

  nlohmann::json output = nlohmann::json::array();
  for (const auto& task : *tasks) {
    output.push_back(task_response_json(task));
  }

  return ToolResponse{
      .tool_use_id = request.id,
      .content = output.dump(2),
  };
}

TaskGetTool::TaskGetTool(TaskManager& manager) : manager_{manager} {}

auto TaskGetTool::name() const -> std::string { return "task_get"; }

auto TaskGetTool::description() const -> std::string { return "Get details for a background task."; }

auto TaskGetTool::is_read_only() const noexcept -> bool { return true; }

auto TaskGetTool::execute(const ToolRequest& request, const ToolContext&) const -> absl::StatusOr<ToolResponse> {
  auto id = parse_task_id(request.parsed_input, "task_get");
  if (!id) {
    return id.error();
  }

  auto record = manager_.get_task(*id);
  if (!record.ok()) {
    return record.status();
  }
  if (!*record) {
    return missing_task_response(request, *id);
  }

  return task_response(request, **record);
}

TaskOutputTool::TaskOutputTool(TaskManager& manager) : manager_{manager} {}

auto TaskOutputTool::name() const -> std::string { return "task_output"; }

auto TaskOutputTool::description() const -> std::string { return "Read the output log for a background task."; }

auto TaskOutputTool::is_read_only() const noexcept -> bool { return true; }

auto TaskOutputTool::execute(const ToolRequest& request, const ToolContext&) const -> absl::StatusOr<ToolResponse> {
  auto id = parse_task_id(request.parsed_input, "task_output");
  if (!id) {
    return id.error();
  }

  auto max_bytes =
      ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(request.parsed_input, "max_bytes", "task_output", 12000);
  if (!max_bytes) {
    return max_bytes.error();
  }
  if (*max_bytes < 1 || *max_bytes > 100000) {
    return absl::StatusOr<ToolResponse>(absl::InvalidArgumentError("task_output max_bytes out of range"));
  }

  auto output = manager_.read_output_tail(*id, static_cast<std::size_t>(*max_bytes));
  if (!output.ok()) {
    return output.status();
  }

  return ToolResponse{
      .tool_use_id = request.id,
      .content = output->empty() ? "(no output)" : std::move(*output),
  };
}

TaskStopTool::TaskStopTool(TaskManager& manager) : manager_{manager} {}

auto TaskStopTool::name() const -> std::string { return "task_stop"; }

auto TaskStopTool::description() const -> std::string { return "Stop a background task."; }

auto TaskStopTool::execute(const ToolRequest& request, const ToolContext&) const -> absl::StatusOr<ToolResponse> {
  auto id = parse_task_id(request.parsed_input, "task_stop");
  if (!id) {
    return id.error();
  }

  auto record = manager_.stop_task(*id);
  if (!record.ok()) {
    return record.status();
  }

  return task_response(request, *record);
}

AgentTool::AgentTool(TaskManager& manager) : manager_{manager} {}

// AgentTool —— "agent" tool 的实现
//
// 这是 LLM 调用 spawn agent 的入口。有两种运行模式：
//
// 模式 A（有 spawn_handler）：
//   调用 CoordinatorRuntime::spawn_agent → 走完整 coordinator 流程
//   （应用 agent definition、创建 team、注册成员等）。
//   这是 agent farm 场景（主 agent 生成子 agent）。
//
// 模式 B（无 spawn_handler）：
//   直接调用 TaskManager::create_agent_task，跳过 coordinator。
//   这是简化场景（例如用户手动创建 agent 任务）。
//
// 为什么两种模式都存在？
//   权限分离：task_tools 单独编译时不知道 coordinator 的存在。
//   register_task_tools 的第二个重载接收 spawn_handler，在 runtime
//   初始化时由 runtime.cpp 注入。
AgentTool::AgentTool(TaskManager& manager, AgentSpawnHandler spawn_handler)
    : manager_{manager}, spawn_handler_{std::move(spawn_handler)} {}

auto AgentTool::name() const -> std::string { return "agent"; }

auto AgentTool::description() const -> std::string { return "Spawn a local background agent task."; }

auto AgentTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return agent_permission_target(request.parsed_input);
}

auto AgentTool::execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> {
  auto input = parse_agent_input(request.parsed_input);
  if (!input.ok()) {
    return input.status();
  }

  if (input->mode != "local_agent") {
    return ToolResponse{
        .tool_use_id = request.id,
        .content = "Invalid mode. Use local_agent.",
        .is_error = true,
    };
  }

  if (spawn_handler_) {
    auto spawned = spawn_handler_(agent_spawn_request_from(*input, context));
    if (!spawned.ok()) {
      return spawned.status();
    }

    const nlohmann::json output = *spawned;
    return ToolResponse{
        .tool_use_id = request.id,
        .content = output.dump(2),
    };
  }

  auto record = manager_.create_agent_task(agent_spec_from(*input, context));
  if (!record.ok()) {
    return record.status();
  }

  const auto agent_id = agent_id_for(*input);
  const auto response = AgentSpawnResponse{
      .agent_id = agent_id,
      .task_id = record->id,
      .backend_type = "subprocess",
      .description = input->description,
      .task = *record,
  };
  const nlohmann::json output = response;

  return ToolResponse{
      .tool_use_id = request.id,
      .content = output.dump(2),
  };
}

auto register_task_tools(ToolRegistry& registry, TaskManager& manager) -> void {
  register_task_tools_impl(registry, manager, {});
}

auto register_task_tools(ToolRegistry& registry, TaskManager& manager, AgentSpawnHandler spawn_handler) -> void {
  register_task_tools_impl(registry, manager, std::move(spawn_handler));
}

}  // namespace codeharness::tasks

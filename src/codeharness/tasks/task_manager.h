#pragma once

#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tasks
{

enum class TaskType
{
    LocalBash,
    LocalAgent,
    RemoteAgent,
};

enum class TaskStatus
{
    Pending,
    Running,
    Completed,
    Failed,
    Killed,
};

struct TaskRecord
{
    std::string id;
    TaskType type = TaskType::LocalBash;
    TaskStatus status = TaskStatus::Pending;
    std::string description;
    std::filesystem::path cwd;
    std::filesystem::path output_file;
    std::optional<std::string> command;
    std::optional<std::string> prompt;
    std::string created_at;
    std::optional<std::string> started_at;
    std::optional<std::string> ended_at;
    std::optional<int> return_code;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
};

struct ShellTaskSpec
{
    std::string description;
    std::filesystem::path cwd;
    std::optional<std::string> command;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
    TaskType type = TaskType::LocalBash;
};

auto task_type_name(TaskType type) -> std::string_view;
auto task_status_name(TaskStatus status) -> std::string_view;
auto parse_task_type(std::string_view value) -> Result<TaskType>;
auto parse_task_status(std::string_view value) -> Result<TaskStatus>;
auto default_task_root() -> Result<std::filesystem::path>;

auto to_json(nlohmann::json& output, const TaskRecord& record) -> void;
auto from_json(const nlohmann::json& input, TaskRecord& record) -> void;

class TaskManager
{
public:
    explicit TaskManager(std::filesystem::path root);
    ~TaskManager();

    TaskManager(const TaskManager&) = delete;
    auto operator=(const TaskManager&) -> TaskManager& = delete;

    TaskManager(TaskManager&&) noexcept;
    auto operator=(TaskManager&&) noexcept -> TaskManager&;

    [[nodiscard]] auto root() const -> const std::filesystem::path&;

    auto create_shell_task(const ShellTaskSpec& spec) -> Result<TaskRecord>;
    auto list_tasks(std::optional<TaskStatus> status = std::nullopt) const -> Result<std::vector<TaskRecord>>;
    auto get_task(std::string_view id) const -> Result<std::optional<TaskRecord>>;
    auto stop_task(std::string_view id) -> Result<TaskRecord>;
    auto read_output_tail(std::string_view id, std::size_t max_bytes = 12000) const -> Result<std::string>;
    auto wait_for_task(std::string_view id) -> Result<TaskRecord>;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace codeharness::tasks

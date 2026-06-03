#include "codeharness/tasks/task_manager.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>
#include <reproc++/reproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "codeharness/core/json_parse.h"
#include "codeharness/core/paths.h"
#include "codeharness/core/shell.h"
#include "codeharness/core/strings.h"
#include "codeharness/core/time.h"
#include "codeharness/tools/text_file.h"

namespace codeharness::tasks
{

namespace
{

constexpr std::size_t kProcessReadChunkSize = 4096;
constexpr auto kTaskRecordExtension = ".json";
constexpr auto kTaskLogExtension = ".log";

auto task_prefix(TaskType type) -> char
{
    switch (type)
    {
    case TaskType::LocalBash: return 'b';
    case TaskType::LocalAgent: return 'a';
    case TaskType::RemoteAgent: return 'r';
    }

    return 't';
}

auto generate_task_id(TaskType type) -> std::string
{
    std::array<unsigned int, 4> bytes{};
    std::random_device random;
    for (auto& byte : bytes)
    {
        byte = random() & 0xFFU;
    }

    std::ostringstream output;
    output << task_prefix(type);
    for (const auto byte : bytes)
    {
        output << std::hex << std::setw(2) << std::setfill('0') << byte;
    }

    return output.str();
}

auto canonical_directory(const std::filesystem::path& path) -> Result<std::filesystem::path>
{
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve task cwd: " + error.message());
    }

    if (!std::filesystem::is_directory(resolved, error))
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "task cwd is not a directory: " + path.string());
    }

    return resolved;
}

template <typename T>
auto expect_json_field(Result<T> value) -> T
{
    if (!value)
    {
        throw nlohmann::json::type_error::create(302, value.error().message, nullptr);
    }

    return std::move(*value);
}

auto read_record_file(const std::filesystem::path& path) -> Result<TaskRecord>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    try
    {
        return nlohmann::json::parse(*content).get<TaskRecord>();
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<TaskRecord>(ErrorKind::InvalidArgument, fmt::format("failed to parse task record: {}", error.what()));
    }
    catch (const nlohmann::json::exception& error)
    {
        return fail<TaskRecord>(ErrorKind::InvalidArgument, fmt::format("failed to parse task record: {}", error.what()));
    }
}

auto terminal_status(TaskStatus status) -> bool
{
    return status == TaskStatus::Completed || status == TaskStatus::Failed || status == TaskStatus::Killed;
}

auto argv_for_spec(const ShellTaskSpec& spec) -> Result<std::vector<std::string>>
{
    if (spec.command && !spec.argv.empty())
    {
        return fail<std::vector<std::string>>(ErrorKind::InvalidArgument, "create_shell_task accepts only one of command or argv");
    }

    if (!spec.command && spec.argv.empty())
    {
        return fail<std::vector<std::string>>(ErrorKind::InvalidArgument, "create_shell_task requires either command or argv");
    }

    if (spec.command)
    {
        const auto command = std::string{trim(*spec.command)};
        if (command.empty())
        {
            return fail<std::vector<std::string>>(ErrorKind::InvalidArgument, "task command is required");
        }

        return default_shell_command_argv(command);
    }

    for (const auto& argument : spec.argv)
    {
        if (argument.empty())
        {
            return fail<std::vector<std::string>>(ErrorKind::InvalidArgument, "task argv entries must not be empty");
        }
    }

    return spec.argv;
}

auto append_available_output(reproc::process& process, std::ofstream& output) -> void
{
    std::array<std::uint8_t, kProcessReadChunkSize> buffer{};

    auto [bytes_read, read_error] = process.read(reproc::stream::out, buffer.data(), buffer.size());
    if (bytes_read > 0)
    {
        output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(bytes_read));
        output.flush();
    }

    if (read_error && read_error != std::errc::resource_unavailable_try_again && read_error != std::errc::operation_would_block && read_error != std::errc::broken_pipe)
    {
        spdlog::warn("failed to read task output: {}", read_error.message());
    }
}

auto record_path_for(const std::filesystem::path& root, std::string_view id) -> std::filesystem::path
{
    return root / (std::string{id} + kTaskRecordExtension);
}

auto output_path_for(const std::filesystem::path& root, std::string_view id) -> std::filesystem::path
{
    return root / (std::string{id} + kTaskLogExtension);
}

} // namespace

auto task_type_name(TaskType type) -> std::string_view
{
    switch (type)
    {
    case TaskType::LocalBash: return "local_bash";
    case TaskType::LocalAgent: return "local_agent";
    case TaskType::RemoteAgent: return "remote_agent";
    }

    return "unknown";
}

auto task_status_name(TaskStatus status) -> std::string_view
{
    switch (status)
    {
    case TaskStatus::Pending: return "pending";
    case TaskStatus::Running: return "running";
    case TaskStatus::Completed: return "completed";
    case TaskStatus::Failed: return "failed";
    case TaskStatus::Killed: return "killed";
    }

    return "unknown";
}

auto parse_task_type(std::string_view value) -> Result<TaskType>
{
    if (value == "local_bash")
    {
        return TaskType::LocalBash;
    }
    if (value == "local_agent")
    {
        return TaskType::LocalAgent;
    }
    if (value == "remote_agent")
    {
        return TaskType::RemoteAgent;
    }

    return fail<TaskType>(ErrorKind::InvalidArgument, "unknown task type: " + std::string{value});
}

auto parse_task_status(std::string_view value) -> Result<TaskStatus>
{
    if (value == "pending")
    {
        return TaskStatus::Pending;
    }
    if (value == "running")
    {
        return TaskStatus::Running;
    }
    if (value == "completed")
    {
        return TaskStatus::Completed;
    }
    if (value == "failed")
    {
        return TaskStatus::Failed;
    }
    if (value == "killed")
    {
        return TaskStatus::Killed;
    }

    return fail<TaskStatus>(ErrorKind::InvalidArgument, "unknown task status: " + std::string{value});
}

auto to_json(nlohmann::json& output, const TaskRecord& record) -> void
{
    output = nlohmann::json{
        {"id", record.id},
        {"type", std::string{task_type_name(record.type)}},
        {"status", std::string{task_status_name(record.status)}},
        {"description", record.description},
        {"cwd", record.cwd.string()},
        {"output_file", record.output_file.string()},
        {"command", optional_to_json(record.command)},
        {"prompt", optional_to_json(record.prompt)},
        {"created_at", record.created_at},
        {"started_at", optional_to_json(record.started_at)},
        {"ended_at", optional_to_json(record.ended_at)},
        {"return_code", optional_to_json(record.return_code)},
        {"metadata", record.metadata},
        {"argv", record.argv},
        {"env", record.env},
    };
}

auto from_json(const nlohmann::json& input, TaskRecord& record) -> void
{
    auto type = parse_task_type(expect_json_field(read_json_field<std::string>(input, "type", "task record")));
    auto status = parse_task_status(expect_json_field(read_json_field<std::string>(input, "status", "task record")));

    record = TaskRecord{
        .id = expect_json_field(read_json_field<std::string>(input, "id", "task record")),
        .type = expect_json_field(std::move(type)),
        .status = expect_json_field(std::move(status)),
        .description = expect_json_field(read_json_field<std::string>(input, "description", "task record")),
        .cwd = std::filesystem::path{expect_json_field(read_json_field<std::string>(input, "cwd", "task record"))},
        .output_file = std::filesystem::path{expect_json_field(read_json_field<std::string>(input, "output_file", "task record"))},
        .command = expect_json_field(read_nullable_optional_json_field<std::string>(input, "command", "task record")),
        .prompt = expect_json_field(read_nullable_optional_json_field<std::string>(input, "prompt", "task record")),
        .created_at = expect_json_field(read_json_field<std::string>(input, "created_at", "task record")),
        .started_at = expect_json_field(read_nullable_optional_json_field<std::string>(input, "started_at", "task record")),
        .ended_at = expect_json_field(read_nullable_optional_json_field<std::string>(input, "ended_at", "task record")),
        .return_code = expect_json_field(read_nullable_optional_json_field<int>(input, "return_code", "task record")),
        .metadata = expect_json_field(read_nullable_json_field<std::map<std::string, std::string>>(input, "metadata", "task record")),
        .argv = expect_json_field(read_nullable_json_field<std::vector<std::string>>(input, "argv", "task record")),
        .env = expect_json_field(read_nullable_json_field<std::map<std::string, std::string>>(input, "env", "task record")),
    };
}

auto default_task_root() -> Result<std::filesystem::path>
{
    const auto data_dir = default_codeharness_data_dir();
    if (!data_dir)
    {
        return fail<std::filesystem::path>(ErrorKind::Config, "home directory is not available");
    }

    return *data_dir / "tasks";
}

struct TaskManager::Impl
{
    explicit Impl(std::filesystem::path root_path) : root{std::move(root_path)}
    {
    }

    ~Impl()
    {
        close();
    }

    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;

    auto persist(const TaskRecord& record) const -> Result<void>
    {
        const nlohmann::json record_json = record;
        auto write_result = atomic_write_text_file(record_path_for(root, record.id), record_json.dump(2));
        if (!write_result)
        {
            return nonstd::make_unexpected(write_result.error());
        }

        return {};
    }

    auto update_record(std::string_view id, const auto& update) -> Result<TaskRecord>
    {
        std::scoped_lock lock{mutex};

        auto iterator = tasks.find(std::string{id});
        if (iterator == tasks.end())
        {
            return fail<TaskRecord>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
        }

        update(iterator->second.record);
        auto persisted = persist(iterator->second.record);
        if (!persisted)
        {
            return nonstd::make_unexpected(persisted.error());
        }

        return iterator->second.record;
    }

    auto load_task(std::string_view id) const -> Result<std::optional<TaskRecord>>
    {
        const auto path = record_path_for(root, id);
        if (!std::filesystem::exists(path))
        {
            return std::optional<TaskRecord>{};
        }

        auto record = read_record_file(path);
        if (!record)
        {
            return nonstd::make_unexpected(record.error());
        }

        return std::optional<TaskRecord>{std::move(*record)};
    }

    auto join_task(std::string_view id) -> Result<void>
    {
        std::thread worker;
        {
            std::scoped_lock lock{mutex};
            auto iterator = tasks.find(std::string{id});
            if (iterator == tasks.end())
            {
                return fail<void>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
            }

            worker = std::move(iterator->second.worker);
        }

        if (worker.joinable())
        {
            worker.join();
        }

        return {};
    }

    auto close() -> void
    {
        std::vector<std::string> running_ids;
        {
            std::scoped_lock lock{mutex};
            for (const auto& [id, state] : tasks)
            {
                if (state.process != nullptr)
                {
                    running_ids.push_back(id);
                }
            }
        }

        for (const auto& id : running_ids)
        {
            auto marked = update_record(id, [](TaskRecord& record) {
                if (!terminal_status(record.status))
                {
                    record.status = TaskStatus::Killed;
                    record.ended_at = utc_timestamp_seconds();
                }
            });
            if (!marked)
            {
                spdlog::warn("failed to mark task as killed during manager close: {}", marked.error().message);
            }

            auto ignored = kill_task(id);
            if (!ignored)
            {
                spdlog::warn("failed to stop task during manager close: {}", ignored.error().message);
            }
        }

        std::vector<std::thread> workers;
        {
            std::scoped_lock lock{mutex};
            for (auto& [_, state] : tasks)
            {
                if (state.worker.joinable())
                {
                    workers.push_back(std::move(state.worker));
                }
            }
        }

        for (auto& worker : workers)
        {
            worker.join();
        }
    }

    auto kill_task(std::string_view id) -> Result<void>
    {
        std::scoped_lock lock{mutex};
        auto iterator = tasks.find(std::string{id});
        if (iterator == tasks.end())
        {
            return fail<void>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
        }

        if (iterator->second.process != nullptr)
        {
            if (auto error = iterator->second.process->kill())
            {
                return fail<void>(ErrorKind::Io, "failed to kill task process: " + error.message());
            }
        }

        return {};
    }

    struct TaskState
    {
        TaskRecord record;
        std::unique_ptr<reproc::process> process;
        std::thread worker;
    };

    std::filesystem::path root;
    mutable std::mutex mutex;
    std::map<std::string, TaskState> tasks;
};

TaskManager::TaskManager(std::filesystem::path root) : impl_{std::make_unique<Impl>(std::move(root))}
{
}

TaskManager::~TaskManager() = default;

TaskManager::TaskManager(TaskManager&&) noexcept = default;

auto TaskManager::operator=(TaskManager&&) noexcept -> TaskManager& = default;

auto TaskManager::root() const -> const std::filesystem::path&
{
    return impl_->root;
}

auto TaskManager::create_shell_task(const ShellTaskSpec& spec) -> Result<TaskRecord>
{
    if (auto mkdir = ensure_directory(impl_->root, "task directory"); !mkdir)
    {
        return nonstd::make_unexpected(mkdir.error());
    }

    auto cwd = canonical_directory(spec.cwd);
    if (!cwd)
    {
        return nonstd::make_unexpected(cwd.error());
    }

    auto argv = argv_for_spec(spec);
    if (!argv)
    {
        return nonstd::make_unexpected(argv.error());
    }

    auto task_id = generate_task_id(spec.type);
    while (std::filesystem::exists(record_path_for(impl_->root, task_id)))
    {
        task_id = generate_task_id(spec.type);
    }

    const auto output_file = output_path_for(impl_->root, task_id);
    auto output_stream = std::ofstream{output_file, std::ios::binary | std::ios::trunc};
    if (!output_stream)
    {
        return fail<TaskRecord>(ErrorKind::Io, "failed to create task output file: " + output_file.string());
    }
    output_stream.close();

    auto record = TaskRecord{
        .id = task_id,
        .type = spec.type,
        .status = TaskStatus::Running,
        .description = std::string{trim(spec.description)},
        .cwd = *cwd,
        .output_file = output_file,
        .command = spec.command,
        .created_at = utc_timestamp_seconds(),
        .started_at = utc_timestamp_seconds(),
        .argv = spec.argv,
        .env = spec.env,
    };

    if (record.description.empty())
    {
        record.description = record.command.value_or(argv->front());
    }

    auto process = std::make_unique<reproc::process>();
    auto cwd_string = record.cwd.string();
    std::vector<std::pair<std::string, std::string>> extra_env;
    extra_env.reserve(record.env.size());
    for (const auto& [name, value] : record.env)
    {
        extra_env.emplace_back(name, value);
    }

    reproc::options options{};
    options.working_directory = cwd_string.c_str();
    options.redirect.in.type = reproc::redirect::discard;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::stdout_;
    options.env.behavior = reproc::env::extend;
    if (!extra_env.empty())
    {
        options.env.extra = reproc::env{extra_env};
    }

    if (auto error = process->start(*argv, options))
    {
        return fail<TaskRecord>(ErrorKind::Io, "failed to start task process: " + error.message());
    }

    auto persisted = impl_->persist(record);
    if (!persisted)
    {
        process->kill();
        process->wait(reproc::milliseconds{5000});
        return nonstd::make_unexpected(persisted.error());
    }

    const auto record_snapshot = record;
    {
        std::scoped_lock lock{impl_->mutex};
        impl_->tasks.emplace(
            record.id,
            Impl::TaskState{
                .record = record_snapshot,
                .process = std::move(process),
            });
    }

    auto worker_id = record_snapshot.id;
    auto worker_root = impl_->root;
    auto* impl = impl_.get();
    {
        std::scoped_lock lock{impl_->mutex};
        auto& state = impl_->tasks.at(worker_id);
        state.worker = std::thread{[impl, worker_id, worker_root] {
            auto output = std::ofstream{output_path_for(worker_root, worker_id), std::ios::binary | std::ios::app};
            if (!output)
            {
                spdlog::warn("failed to open task output log for {}", worker_id);
            }

            reproc::process* process = nullptr;
            {
                std::scoped_lock lock{impl->mutex};
                auto iterator = impl->tasks.find(worker_id);
                if (iterator == impl->tasks.end())
                {
                    return;
                }
                process = iterator->second.process.get();
            }

            int exit_status = -1;
            std::error_code wait_error;
            while (true)
            {
                auto [events, poll_error] = process->poll(reproc::event::out | reproc::event::exit, reproc::milliseconds{50});
                if (poll_error)
                {
                    wait_error = poll_error;
                    break;
                }

                if (output && (events & reproc::event::out) != 0)
                {
                    append_available_output(*process, output);
                }

                if ((events & reproc::event::exit) == 0)
                {
                    continue;
                }

                std::tie(exit_status, wait_error) = process->wait(reproc::milliseconds{0});
                break;
            }

            if (output)
            {
                append_available_output(*process, output);
            }

            auto updated = impl->update_record(worker_id, [&](TaskRecord& current) {
                if (wait_error)
                {
                    current.status = TaskStatus::Failed;
                    current.metadata["wait_error"] = wait_error.message();
                }
                else if (current.status == TaskStatus::Killed)
                {
                    current.return_code = exit_status;
                }
                else
                {
                    current.return_code = exit_status;
                    current.status = exit_status == 0 ? TaskStatus::Completed : TaskStatus::Failed;
                }

                current.ended_at = utc_timestamp_seconds();
            });
            if (!updated)
            {
                spdlog::warn("failed to persist completed task {}: {}", worker_id, updated.error().message);
            }

            std::scoped_lock lock{impl->mutex};
            auto iterator = impl->tasks.find(worker_id);
            if (iterator != impl->tasks.end())
            {
                iterator->second.process.reset();
            }
        }};
    }

    return record_snapshot;
}

auto TaskManager::list_tasks(std::optional<TaskStatus> status) const -> Result<std::vector<TaskRecord>>
{
    auto mkdir = ensure_directory(impl_->root, "task directory");
    if (!mkdir)
    {
        return nonstd::make_unexpected(mkdir.error());
    }

    std::map<std::string, TaskRecord> records;
    {
        std::scoped_lock lock{impl_->mutex};
        for (const auto& [id, state] : impl_->tasks)
        {
            records.emplace(id, state.record);
        }
    }

    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator{impl_->root, error})
    {
        if (error)
        {
            return fail<std::vector<TaskRecord>>(ErrorKind::Io, "failed to scan task directory: " + error.message());
        }

        const auto path = entry.path();
        if (!entry.is_regular_file(error) || path.extension() != kTaskRecordExtension)
        {
            continue;
        }

        auto record = read_record_file(path);
        if (!record)
        {
            return nonstd::make_unexpected(record.error());
        }

        records.insert_or_assign(record->id, std::move(*record));
    }

    std::vector<TaskRecord> result;
    for (auto& [_, record] : records)
    {
        if (!status || record.status == *status)
        {
            result.push_back(std::move(record));
        }
    }

    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.created_at == right.created_at)
        {
            return left.id > right.id;
        }

        return left.created_at > right.created_at;
    });

    return result;
}

auto TaskManager::get_task(std::string_view id) const -> Result<std::optional<TaskRecord>>
{
    {
        std::scoped_lock lock{impl_->mutex};
        auto iterator = impl_->tasks.find(std::string{id});
        if (iterator != impl_->tasks.end())
        {
            return std::optional<TaskRecord>{iterator->second.record};
        }
    }

    return impl_->load_task(id);
}

auto TaskManager::stop_task(std::string_view id) -> Result<TaskRecord>
{
    auto current = get_task(id);
    if (!current)
    {
        return nonstd::make_unexpected(current.error());
    }
    if (!*current)
    {
        return fail<TaskRecord>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
    }

    if (terminal_status((*current)->status))
    {
        return **current;
    }

    auto updated = impl_->update_record(id, [](TaskRecord& record) {
        record.status = TaskStatus::Killed;
        record.ended_at = utc_timestamp_seconds();
    });
    if (!updated)
    {
        return nonstd::make_unexpected(updated.error());
    }

    auto killed = impl_->kill_task(id);
    if (!killed)
    {
        return nonstd::make_unexpected(killed.error());
    }

    auto joined = impl_->join_task(id);
    if (!joined)
    {
        return nonstd::make_unexpected(joined.error());
    }

    auto final_record = get_task(id);
    if (!final_record)
    {
        return nonstd::make_unexpected(final_record.error());
    }
    if (!*final_record)
    {
        return fail<TaskRecord>(ErrorKind::Internal, "task disappeared after stop: " + std::string{id});
    }

    return **final_record;
}

auto TaskManager::read_output_tail(std::string_view id, std::size_t max_bytes) const -> Result<std::string>
{
    auto task = get_task(id);
    if (!task)
    {
        return nonstd::make_unexpected(task.error());
    }
    if (!*task)
    {
        return fail<std::string>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
    }

    std::ifstream file{(*task)->output_file, std::ios::binary};
    if (!file)
    {
        return fail<std::string>(ErrorKind::Io, "failed to open task output: " + (*task)->output_file.string());
    }

    file.seekg(0, std::ios::end);
    const auto end_position = file.tellg();
    const auto size = static_cast<std::streamoff>(end_position);
    if (size <= 0 || max_bytes == 0)
    {
        return std::string{};
    }

    const auto bytes_to_read = static_cast<std::streamoff>(std::min<std::uintmax_t>(static_cast<std::uintmax_t>(max_bytes), static_cast<std::uintmax_t>(size)));
    file.seekg(size - bytes_to_read, std::ios::beg);

    std::string result;
    result.resize(static_cast<std::size_t>(bytes_to_read));
    file.read(result.data(), static_cast<std::streamsize>(bytes_to_read));

    if (!file.good() && !file.eof())
    {
        return fail<std::string>(ErrorKind::Io, "failed to read task output: " + (*task)->output_file.string());
    }

    return result;
}

auto TaskManager::wait_for_task(std::string_view id) -> Result<TaskRecord>
{
    auto joined = impl_->join_task(id);
    if (!joined)
    {
        return nonstd::make_unexpected(joined.error());
    }

    auto task = get_task(id);
    if (!task)
    {
        return nonstd::make_unexpected(task.error());
    }
    if (!*task)
    {
        return fail<TaskRecord>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
    }

    return **task;
}

} // namespace codeharness::tasks

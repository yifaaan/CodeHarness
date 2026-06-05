//==============================================================================
// task_manager.cpp — 任务管理系统
//
// 架构角色：核心服务层
// 职责：管理子进程（subprocess）的创建、执行、监控、生命周期。
//       它是 CodeHarness 中 agent spawning 体系的最底层基础设施。
//
// 核心概念：
//   1. TaskRecord（任务记录）—— 持久化到磁盘 JSON 文件的任务状态快照。
//      每次状态变更后写回文件，保证进程重启后能从磁盘恢复。
//   2. TaskState（运行时状态）—— 进程运行时内存中的状态，包含：
//       - record: 当前记录
//       - process: reproc 子进程句柄
//       - worker: 后台监视线程
//       - stop_requested: atomic 标志，实现"协作者请求停止 → worker 自行处理"
//   3. 线程模型：
//       - 调用方线程：创建任务、读取状态、请求停止
//       - worker 线程：独占 process->poll/read/wait/kill 操作
//       - 使用 mutex 保护 tasks map，使用 atomic 跨线程传递停止信号
//
// 设计原理：
//   - worker 线程独立 poll：子进程的 stdout 被管道捕获，worker 线程
//     以 50ms 间隔 poll，将输出写入 .log 文件。这样调用方不必阻塞等待。
//   - stop_requested atomic：调用方不直接调用 process->kill()，而是
//     设置 atomic 标志让 worker 线程自行 kill+wait。这避免了竞态——
//     worker 正在 poll 时如果调用方 kill+wait，会产生双重 wait 或
//     管道读取混乱。
//   - 幂等停止：如果任务已自然结束（Completed/Failed），stop_task()
//     直接返回当前状态，不再标记 Killed。
//   - 持久化在锁外执行：update_record 在锁内生成快照，然后解锁再写文件，
//     避免磁盘 I/O 长锁阻塞整个 tasks map。
//   - TaskManager 析构时会清理所有 running 任务：先标记 Killed，再请求
//     stop，最后 join 所有 worker 线程。
//
// 新人提示：这是整个项目中最"传统"的系统编程代码——多线程、进程管理、
// 文件 I/O。其他模块（coordinator/task_tools）在此基础上提供更高级的
// agent 抽象。理解这里的线程模型是理解 agent spawn 的关键。
//==============================================================================

#include "codeharness/tasks/task_manager.h"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>
#include <reproc++/reproc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
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

// worker 线程每次从进程管道读取的块大小（4KB）
constexpr std::size_t kProcessReadChunkSize = 4096;
constexpr auto kTaskRecordExtension = ".json";
constexpr auto kTaskLogExtension = ".log";

// 根据任务类型生成 ID 前缀字符：
//   'b' = local_bash（本地 shell 命令）
//   'a' = local_agent（本地子 agent 进程）
//   'r' = remote_agent（预留，未来支持远程 agent）
// 后续拼接 4 字节随机十六进制，形成唯一 ID（如 "a3f7a1b2"）
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

// generate_task_id —— 生成唯一任务 ID
//
// 原理：1 字节类型前缀 + 4 字节随机数 → 9 字符 ASCII 字符串。
// 使用 std::random_device（真随机数生成器）而非伪随机，
// 避免长时间运行后的 ID 碰撞概率升高。
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

auto default_agent_argv(const AgentTaskSpec& spec, const std::filesystem::path& cwd) -> std::vector<std::string>
{
    auto argv = std::vector<std::string>{
        "codeharness",
        "-p",
        spec.prompt,
        "--cwd",
        cwd.string(),
    };

    return argv;
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

// TaskManager::Impl（PIMPL 模式）
//
// PIMPL（Pointer to Implementation）是 C++ 惯用设计模式：
// 将实现细节放到 .cpp 文件的私有 impl 类中，头文件只暴露不完整类型指针。
// 好处：
//   1. 编译期隔离——头文件改 Impl 细节不触发依赖者重编译
//   2. 实现隐藏——TaskState 等内幕完全对调用方不可见
//   3. 接口稳定——公共头文件只需前向声明、不暴露内部 include
//
// 代价：所有成员访问多一层指针间接。但在 agent runtime 这种启动一次、
// 持续运行的场景中，这层间接完全可以接受。
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

    // persist —— 将 TaskRecord 持久化到磁盘 JSON 文件
    //
    // 路径：{root}/{task_id}.json
    // 用 atomic_write_text_file（先写临时文件再 rename）保证写入原子性，
    // 避免进程崩溃时产生半写文件。
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

    // update_record —— 线程安全地更新任务状态
    //
    // 参数 update 是一个泛型 lambda/函数，接受 TaskRecord&，在里面做修改。
    // 步骤：
    //   1. 加锁找到对应的 TaskState
    //   2. 在锁内执行 update(record) 修改内存状态
    //   3. 取出快照后释放锁
    //   4. 在锁外执行文件持久化（避免 I/O 延迟阻塞其他 task 操作）
    auto update_record(std::string_view id, const auto& update) -> Result<TaskRecord>
    {
        TaskRecord snapshot;
        {
            std::scoped_lock lock{mutex};

            auto iterator = tasks.find(std::string{id});
            if (iterator == tasks.end())
            {
                return fail<TaskRecord>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
            }

            update(iterator->second.record);
            snapshot = iterator->second.record;
        }

        // 持久化在锁外执行，避免磁盘 I/O 阻塞所有 task map 操作
        auto persisted = persist(snapshot);
        if (!persisted)
        {
            return nonstd::make_unexpected(persisted.error());
        }

        return snapshot;
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

    // join_task —— 等待 worker 线程结束
    //
    // 注意：锁内只移动 worker thread，解锁后在实际调用线程上 join。
    // 这样不会在持锁时阻塞（join 可能等很久）。
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

            // 请求 worker 自行终止进程，而不是直接调用 process->kill()
            auto ignored = request_stop(id);
            if (!ignored)
            {
                spdlog::warn("failed to request task stop during manager close: {}", ignored.error().message);
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

    // 停止请求：通过 atomic 标志通知 worker 线程
    //
    // 不再直接调用 process->kill()。
    // 而是设置 stop_requested = true，worker 线程在 poll 循环中检查到后，
    // 由 worker 自己调用 process->kill() 和 process->wait()。
    auto request_stop(std::string_view id) -> Result<void>
    {
        std::scoped_lock lock{mutex};
        auto iterator = tasks.find(std::string{id});
        if (iterator == tasks.end())
        {
            return fail<void>(ErrorKind::InvalidArgument, "No task found with ID: " + std::string{id});
        }

        if (iterator->second.stop_requested.load(std::memory_order_relaxed))
        {
            return {}; // 已经请求过停止，幂等
        }

        iterator->second.stop_requested.store(true, std::memory_order_release);
        return {};
    }

    // TaskState —— 每个 task 的运行时状态
    //   - stop_requested 是 atomic，worker 线程在每次 poll 前检查它
    //   - worker 线程独占 process 的所有操作（poll/read/wait/kill）
    //   - 调用方线程只设置 stop_requested
    struct TaskState
    {
        TaskRecord record;
        std::unique_ptr<reproc::process> process;
        std::thread worker;
        std::atomic<bool> stop_requested{false}; // 调用方设置，worker 读取
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

// === TaskManager 对外开放接口 ==============================================

// create_shell_task —— 创建并启动一个 shell 子进程任务
//
// 完整流程：
//   1. 确保任务目录存在
//   2. 解析工作目录为绝对路径（canonical_directory）
//   3. 解析命令或 argv（argv_for_spec）
//   4. 生成唯一 task ID（首字符按类型区分，避免跨类型碰撞）
//   5. 创建空输出 .log 文件
//   6. 构造 TaskRecord（初始状态 Running）
//   7. 启动 reproc 子进程（管道重定向 stdout+stderr）
//   8. 持久化 TaskRecord 到 JSON 文件
//   9. 注册到 in-memory tasks map
//  10. 创建 worker 线程开始轮询子进程输出
//
// 关键设计决策：worker 线程为何不直接用阻塞 read？
//   因为我们需要同时监听 process exit 和管道可读两种事件。
// reproc::poll 支持 select-like 的多事件等待（out | exit），
// 而阻塞 read 无法区分"进程结束"和"管道暂时无数据"。
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
        .prompt = spec.prompt,
        .created_at = utc_timestamp_seconds(),
        .started_at = utc_timestamp_seconds(),
        .metadata = spec.metadata,
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
        if (impl_->tasks.find(record.id) != impl_->tasks.end())
        {
            process->kill();
            process->wait(reproc::milliseconds{5000});
            return fail<TaskRecord>(ErrorKind::Internal,
                "task id collision: " + record.id);
        }

        auto& state = impl_->tasks[record.id];
        state.record = record_snapshot;
        state.process = std::move(process);
    }

    auto worker_id = record_snapshot.id;
    auto worker_root = impl_->root;
    auto* impl = impl_.get();
    {
        std::scoped_lock lock{impl_->mutex};
        auto& state = impl_->tasks.at(worker_id);
        state.worker = std::thread{[impl, worker_id, worker_root] {
            // === worker 线程 ===
            // 这个 lambda 在一个独立线程中运行，生命周期等于子进程生命周期。
            // 职责：
            //   1. 以 50ms 间隔 poll 子进程（out / exit 事件）
            //   2. 将 stdout 输出附加写入 .log 文件
            //   3. 检查 stop_requested，收到后执行 kill+wait
            //   4. 进程退出后更新 TaskRecord 为 Completed/Failed
            //   5. 清理 process 指针（重置 unique_ptr）
            //
            // 线程安全约定：
            //   - 读取 process/stop_flag：在锁内抓取原始指针，后续都在锁外使用
            //   - 写 tasks map：通过 Impl::update_record（内部加锁）
            //   - process->kill() 等操作只在 worker 线程调用。调用方线程只设
            //     stop_requested atomic，绝不碰 process 指针。
            auto output = std::ofstream{output_path_for(worker_root, worker_id), std::ios::binary | std::ios::app};
            if (!output)
            {
                spdlog::warn("failed to open task output log for {}", worker_id);
            }

            reproc::process* process = nullptr;
            std::atomic<bool>* stop_flag = nullptr;
            {
                std::scoped_lock lock{impl->mutex};
                auto iterator = impl->tasks.find(worker_id);
                if (iterator == impl->tasks.end())
                {
                    return;
                }
                process = iterator->second.process.get();
                stop_flag = &iterator->second.stop_requested;
            }

            int exit_status = -1;
            std::error_code wait_error;
            bool was_killed = false;
            while (true)
            {
                // 步骤 A：检查停止请求
                // 在 poll 之前检查 stop_requested。如果调用方设置了该标志，
                // 由 worker 线程（而非调用方线程）执行 process->kill()。
                // memory_order_acquire 保证看到调用方 store(..., release)
                // 之前的所有写入——即 stop_requested=true 时，调用方对
                // tasks map 的修改也已可见。
                if (stop_flag->load(std::memory_order_acquire))
                {
                    // 在 kill 之前尽量刷出管道中缓冲的输出
                    if (output)
                    {
                        append_available_output(*process, output);
                    }
                    if (auto kill_error = process->kill())
                    {
                        spdlog::warn("failed to kill task {}: {}", worker_id, kill_error.message());
                    }
                    std::tie(exit_status, wait_error) = process->wait(reproc::milliseconds{5000});
                    was_killed = true;
                    break;
                }

                // 步骤 B：轮询子进程事件
                // reproc::poll 同时监听两种事件：
                //   - out: stdout 可读（有新增输出）
                //   - exit: 子进程已退出
                // 超时 50ms，让 worker 线程可以及时响应 stop_requested。
                // 如果只阻塞 read，stop_requested 可能延迟很久才被检查。
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

            // 刷出剩余输出（kill 路径跳过：进程已死，pipe 可能不可读）
            if (!was_killed && output)
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

            // worker 线程独占清理——没有其他线程在此时访问 process
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

// create_agent_task —— 创建 agent 子进程任务
//
// Agent 任务本质上是特殊的 shell 任务：启动一个新的 codeharness 实例
// 作为子 agent。关键逻辑：
//   1. 使用 prompt 而非 command——子 agent 接收的是自然语言指令
//   2. 如果未指定 command/argv，使用 default_agent_argv 构造默认参数：
//      codeharness -p "<prompt>" --cwd "<cwd>"
//   3. model 等信息存入 metadata，不作为直接参数（由子 agent 自行解析）
//   4. 类型固定为 TaskType::LocalAgent
//
// 为什么用 subprocess 实现 agent spawn？
//   这是"进程级隔离"的方式。每个 agent 运行在独立子进程中，拥有自己的
//   内存空间。如果有子 agent 崩溃，不会影响主 agent。代价是启动成本
//   稍高（约 50-100ms），但对于 coding agent 的长时间运行场景可接受。
auto TaskManager::create_agent_task(const AgentTaskSpec& spec) -> Result<TaskRecord>
{
    const auto prompt = std::string{trim(spec.prompt)};
    if (prompt.empty())
    {
        return fail<TaskRecord>(ErrorKind::InvalidArgument, "create_agent_task requires prompt");
    }

    auto cwd = canonical_directory(spec.cwd);
    if (!cwd)
    {
        return nonstd::make_unexpected(cwd.error());
    }

    auto command = spec.command;
    auto argv = spec.argv;
    if (!command && argv.empty())
    {
        argv = default_agent_argv(spec, *cwd);
    }

    auto metadata = spec.metadata;
    if (spec.model)
    {
        metadata["model"] = *spec.model;
    }

    return create_shell_task(
        ShellTaskSpec{
            .description = spec.description,
            .cwd = *cwd,
            .command = std::move(command),
            .prompt = prompt,
            .argv = std::move(argv),
            .env = spec.env,
            .metadata = std::move(metadata),
            .type = TaskType::LocalAgent,
        });
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
    auto dir_iter = std::filesystem::directory_iterator{impl_->root, error};
    if (error)
    {
        return fail<std::vector<TaskRecord>>(ErrorKind::Io, "failed to scan task directory: " + error.message());
    }

    for (const auto& entry : dir_iter)
    {
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

    // 仅在任务尚未自然结束时才标记为 Killed。
    // 如果 worker 线程在 get_task 和 update_record 之间已经将状态设为
    // Completed/Failed，就不再覆盖。
    auto updated = impl_->update_record(id, [](TaskRecord& record) {
        if (!terminal_status(record.status))
        {
            record.status = TaskStatus::Killed;
            record.ended_at = utc_timestamp_seconds();
        }
    });
    if (!updated)
    {
        return nonstd::make_unexpected(updated.error());
    }

    // 请求 worker 自行终止进程（而非直接 kill）
    auto stopped = impl_->request_stop(id);
    if (!stopped)
    {
        return nonstd::make_unexpected(stopped.error());
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
    if (!file)
    {
        return fail<std::string>(ErrorKind::Io, "failed to seek output file: " + (*task)->output_file.string());
    }

    const auto end_position = file.tellg();
    if (end_position == static_cast<std::streampos>(-1))
    {
        return fail<std::string>(ErrorKind::Io, "failed to read output file size: " + (*task)->output_file.string());
    }

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

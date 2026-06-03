#include "test_support.h"

#include "codeharness/tasks/task_manager.h"

namespace
{

auto direct_test_argv(std::string text) -> std::vector<std::string>
{
    return {std::string{CODEHARNESS_CMAKE_COMMAND}, "-E", "echo_append", std::move(text)};
}

auto sleeper_argv() -> std::vector<std::string>
{
    return {std::string{CODEHARNESS_CMAKE_COMMAND}, "-E", "sleep", "30"};
}

} // namespace

TEST_CASE("task record serializes through nlohmann json interface")
{
    auto record = codeharness::tasks::TaskRecord{
        .id = "b12345678",
        .type = codeharness::tasks::TaskType::LocalBash,
        .status = codeharness::tasks::TaskStatus::Completed,
        .description = "json task",
        .cwd = std::filesystem::path{"work"},
        .output_file = std::filesystem::path{"work"} / "b12345678.log",
        .command = std::string{"echo hi"},
        .created_at = "2026-06-03T00:00:00Z",
        .return_code = 0,
        .metadata = {{"kind", "test"}},
        .argv = {"cmd", "arg"},
        .env = {{"A", "B"}},
    };

    const nlohmann::json json = record;
    CHECK(json.at("type") == "local_bash");
    CHECK(json.at("status") == "completed");
    CHECK(json.at("prompt").is_null());
    CHECK(json.at("return_code") == 0);

    auto parsed = json.get<codeharness::tasks::TaskRecord>();
    CHECK(parsed.id == record.id);
    CHECK(parsed.type == record.type);
    CHECK(parsed.status == record.status);
    CHECK(parsed.command == record.command);
    CHECK(!parsed.prompt.has_value());
    CHECK(parsed.return_code == record.return_code);
    CHECK(parsed.metadata == record.metadata);
    CHECK(parsed.argv == record.argv);
    CHECK(parsed.env == record.env);
}

TEST_CASE("task manager creates shell task persists record and reads output")
{
    TempDir temp{"codeharness-task-create-test"};
    codeharness::tasks::TaskManager manager{temp.path / "tasks"};

    auto task = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "hello task",
            .cwd = temp.path,
            .argv = direct_test_argv("hello-task"),
        });

    if (!task)
    {
        FAIL(task.error().message);
    }
    REQUIRE(task.has_value());
    CHECK(task->status == codeharness::tasks::TaskStatus::Running);
    CHECK(task->type == codeharness::tasks::TaskType::LocalBash);
    CHECK(task->description == "hello task");

    auto completed = manager.wait_for_task(task->id);
    REQUIRE(completed.has_value());
    CHECK(completed->status == codeharness::tasks::TaskStatus::Completed);
    CHECK(completed->return_code == 0);
    CHECK(std::filesystem::exists(completed->output_file));
    CHECK(std::filesystem::exists(manager.root() / (completed->id + ".json")));

    auto output = manager.read_output_tail(completed->id);
    REQUIRE(output.has_value());
    CHECK(output->find("hello-task") != std::string::npos);

    codeharness::tasks::TaskManager reloaded{temp.path / "tasks"};
    auto listed = reloaded.list_tasks();
    if (!listed)
    {
        FAIL(listed.error().message);
    }
    REQUIRE(listed.has_value());
    REQUIRE(listed->size() == 1);
    CHECK(listed->front().id == completed->id);
    CHECK(listed->front().status == codeharness::tasks::TaskStatus::Completed);
}

TEST_CASE("task manager reports failed shell task status")
{
    TempDir temp{"codeharness-task-failed-test"};
    codeharness::tasks::TaskManager manager{temp.path / "tasks"};

    auto task = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "fail task",
            .cwd = temp.path,
#ifdef _WIN32
            .argv = {"cmd.exe", "/c", "exit 7"},
#else
            .argv = {"/bin/sh", "-c", "exit 7"},
#endif
        });

    if (!task)
    {
        FAIL(task.error().message);
    }
    REQUIRE(task.has_value());

    auto completed = manager.wait_for_task(task->id);
    REQUIRE(completed.has_value());
    CHECK(completed->status == codeharness::tasks::TaskStatus::Failed);
    CHECK(completed->return_code == 7);
}

TEST_CASE("task manager stops running task")
{
    TempDir temp{"codeharness-task-stop-test"};
    codeharness::tasks::TaskManager manager{temp.path / "tasks"};

    auto task = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "sleeper",
            .cwd = temp.path,
            .argv = sleeper_argv(),
        });

    if (!task)
    {
        FAIL(task.error().message);
    }
    REQUIRE(task.has_value());

    auto stopped = manager.stop_task(task->id);
    REQUIRE(stopped.has_value());
    CHECK(stopped->status == codeharness::tasks::TaskStatus::Killed);
    CHECK(stopped->ended_at.has_value());

    auto reloaded = manager.get_task(task->id);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->has_value());
    CHECK((*reloaded)->status == codeharness::tasks::TaskStatus::Killed);
}

TEST_CASE("task manager destructor stops running tasks")
{
    TempDir temp{"codeharness-task-close-test"};
    std::string task_id;

    {
        codeharness::tasks::TaskManager manager{temp.path / "tasks"};
        auto task = manager.create_shell_task(
            codeharness::tasks::ShellTaskSpec{
                .description = "close sleeper",
                .cwd = temp.path,
                .argv = sleeper_argv(),
            });

        if (!task)
        {
            FAIL(task.error().message);
        }
        REQUIRE(task.has_value());
        task_id = task->id;
    }

    codeharness::tasks::TaskManager reloaded{temp.path / "tasks"};
    auto task = reloaded.get_task(task_id);
    REQUIRE(task.has_value());
    REQUIRE(task->has_value());
    CHECK((*task)->status == codeharness::tasks::TaskStatus::Killed);
}

TEST_CASE("task manager reads only output tail")
{
    TempDir temp{"codeharness-task-tail-test"};
    codeharness::tasks::TaskManager manager{temp.path / "tasks"};

    auto task = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "tail task",
            .cwd = temp.path,
            .argv = direct_test_argv("abcdefghijklmnopqrstuvwxyz"),
        });

    if (!task)
    {
        FAIL(task.error().message);
    }
    REQUIRE(task.has_value());
    auto completed = manager.wait_for_task(task->id);
    REQUIRE(completed.has_value());

    auto tail = manager.read_output_tail(task->id, 5);
    REQUIRE(tail.has_value());
    CHECK(tail->size() == 5);
    CHECK(tail->find("vwxyz") != std::string::npos);
}

TEST_CASE("task manager validates command and argv selection")
{
    TempDir temp{"codeharness-task-validation-test"};
    codeharness::tasks::TaskManager manager{temp.path / "tasks"};

    auto neither = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "empty",
            .cwd = temp.path,
        });
    REQUIRE(!neither.has_value());
    CHECK(neither.error().message == "create_shell_task requires either command or argv");

    auto both = manager.create_shell_task(
        codeharness::tasks::ShellTaskSpec{
            .description = "both",
            .cwd = temp.path,
            .command = std::string{"echo hi"},
            .argv = direct_test_argv("hi"),
        });
    REQUIRE(!both.has_value());
    CHECK(both.error().message == "create_shell_task accepts only one of command or argv");
}

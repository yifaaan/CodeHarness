#include "test_support.h"

#include <git2.h>

TempDir::TempDir(std::string name) : path{std::filesystem::temp_directory_path() / std::move(name)}
{
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
    std::filesystem::create_directories(path);
}

TempDir::~TempDir()
{
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

auto set_request_input(codeharness::ToolRequest& request, std::string input_json) -> void
{
    request.input_json = std::move(input_json);
    request.parsed_input = nlohmann::json::parse(request.input_json);
}

auto read_file_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{path, std::ios::binary};

    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

auto write_file(const std::filesystem::path& path, const std::string& content) -> void
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path);
    REQUIRE(file.is_open());
    file << content;
}

namespace
{

struct GitTestRuntime
{
    GitTestRuntime()
    {
        git_libgit2_init();
    }

    ~GitTestRuntime()
    {
        git_libgit2_shutdown();
    }

    GitTestRuntime(const GitTestRuntime&) = delete;
    auto operator=(const GitTestRuntime&) -> GitTestRuntime& = delete;
};

constexpr auto kGitTestRepositoryDeleter = [](git_repository* repository) noexcept { git_repository_free(repository); };

} // namespace

auto init_git_repository_with_head(const std::filesystem::path& repo, std::string_view branch) -> void
{
    GitTestRuntime runtime;

    git_repository* raw_repository = nullptr;
    REQUIRE(git_repository_init(&raw_repository, repo.string().c_str(), 0) == 0);

    const std::unique_ptr<git_repository, decltype(kGitTestRepositoryDeleter)> repository{
        raw_repository,
        kGitTestRepositoryDeleter,
    };
    const auto head_name = std::string{"refs/heads/"} + std::string{branch};
    REQUIRE(git_repository_set_head(repository.get(), head_name.c_str()) == 0);
}

auto WriteFileRequestProvider::stream(
    std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
    -> codeharness::Result<void>
{
    for (auto& message : messages)
    {
        if (message.role != codeharness::Role::Tool)
        {
            continue;
        }

        for (auto& block : message.content)
        {
            if (auto result = std::get_if<codeharness::ToolResultBlock>(&block))
            {
                sink(codeharness::AssistantTextDelta{result->content});
                sink(codeharness::MessageFinished{});
                return {};
            }
        }
    }

    sink(
        codeharness::ToolUseStarted{
            .id = "tool-use-1",
            .name = "write_file",
        });
    sink(
        codeharness::ToolUseInputDelta{
            .id = "tool-use-1",
            .input_json_delta = R"({"path":"output.txt","content":"hello"})",
        });
    sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
    sink(codeharness::MessageFinished{});

    return {};
}
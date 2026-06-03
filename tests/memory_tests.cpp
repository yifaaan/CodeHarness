#include "test_support.h"

TEST_CASE("project memory dir uses injected root and stable hash")
{
    TempDir temp{"codeharness-memory-path-test"};
    const auto repo = temp.path / "repo";
    const auto root = temp.path / "memory-root";
    std::filesystem::create_directories(repo);

    auto path = codeharness::memory::project_memory_dir(repo, root);
    auto again = codeharness::memory::project_memory_dir(repo, root);

    REQUIRE(path.has_value());
    REQUIRE(again.has_value());
    CHECK(*path == *again);
    CHECK(path->parent_path() == root);
    CHECK(path->filename().string().starts_with("repo-"));
    CHECK(path->filename().string().size() == std::string{"repo-"}.size() + 12);
}

TEST_CASE("memory store adds and scans markdown metadata")
{
    TempDir temp{"codeharness-memory-add-scan-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};

    auto added = store.add(
        codeharness::memory::AddMemoryRequest{
            .title = "Build Notes",
            .body = "Use CMake.\nDo not generate go.mod.",
            .tags = {"build", "cmake"},
        });

    REQUIRE(added.has_value());
    CHECK(added->title == "Build Notes");
    CHECK(added->metadata.type == "project");
    CHECK(added->metadata.scope == "project");
    CHECK(added->metadata.category == "knowledge");
    CHECK(added->metadata.importance == 1);
    CHECK(added->metadata.tags == std::vector<std::string>{"build", "cmake"});

    auto memories = store.scan();
    REQUIRE(memories.has_value());
    REQUIRE(memories->size() == 1);
    CHECK(memories->front().title == "Build Notes");
    CHECK(memories->front().description == "Use CMake.");

    const auto raw = read_file_text(added->path);
    CHECK(raw.find("schema_version: 1") != std::string::npos);
    CHECK(raw.find("source: \"manual\"") != std::string::npos);
    CHECK(raw.find("Use CMake.") != std::string::npos);
}

TEST_CASE("memory store refreshes duplicate content by signature")
{
    TempDir temp{"codeharness-memory-duplicate-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};

    auto first = store.add(
        codeharness::memory::AddMemoryRequest{
            .title = "Build Notes",
            .body = "Use CMake for builds.",
        });
    auto second = store.add(
        codeharness::memory::AddMemoryRequest{
            .title = "Build Notes Updated",
            .body = "Use CMake for builds.",
        });

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->path == second->path);

    auto memories = store.scan();
    REQUIRE(memories.has_value());
    REQUIRE(memories->size() == 1);
    CHECK(memories->front().title == "Build Notes Updated");
    CHECK(memories->front().metadata.id == first->metadata.id);
}

TEST_CASE("memory store soft removes entries and updates index")
{
    TempDir temp{"codeharness-memory-remove-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};

    auto added = store.add(
        codeharness::memory::AddMemoryRequest{
            .title = "Build Notes",
            .body = "Use CMake.",
        });
    REQUIRE(added.has_value());

    auto removed = store.soft_remove(added->metadata.id);

    REQUIRE(removed.has_value());
    CHECK(*removed);

    auto visible = store.scan();
    REQUIRE(visible.has_value());
    CHECK(visible->empty());

    auto all = store.scan(codeharness::memory::MemoryScanOptions{.include_disabled = true});
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 1);
    CHECK(all->front().metadata.disabled);

    const auto index_text = read_file_text(store.root() / "MEMORY.md");
    CHECK(index_text.find(added->relative_path.string()) == std::string::npos);
}

TEST_CASE("memory store search ranks relevant memories")
{
    TempDir temp{"codeharness-memory-search-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};

    REQUIRE(store
                .add(
                    codeharness::memory::AddMemoryRequest{
                        .title = "Build Notes",
                        .body = "Use CMake as the build tool.",
                    })
                .has_value());
    REQUIRE(store
                .add(
                    codeharness::memory::AddMemoryRequest{
                        .title = "Provider Notes",
                        .body = "Anthropic streaming returns deltas.",
                    })
                .has_value());

    auto results = store.search("cmake build", 5);

    REQUIRE(results.has_value());
    REQUIRE(!results->empty());
    CHECK(results->front().header.title == "Build Notes");
    CHECK(results->front().body.find("Use CMake") != std::string::npos);
}

TEST_CASE("memory slash command adds lists searches and removes entries")
{
    TempDir temp{"codeharness-memory-command-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};
    codeharness::SkillRegistry skills;
    auto commands = codeharness::build_builtin_command_registry(
        skills, codeharness::BuiltinCommandRegistryOptions{.memory_store = &store});

    auto invalid = codeharness::execute_slash_command(commands, "/memory add missing separator");
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().message == "usage: /memory add TITLE :: BODY");

    auto added = codeharness::execute_slash_command(commands, "/memory add Build Notes :: Use CMake for builds");
    REQUIRE(added.has_value());
    REQUIRE(added->message.has_value());
    CHECK(added->message->find("Added memory: Build Notes") != std::string::npos);

    auto listed = codeharness::execute_slash_command(commands, "/memory list");
    REQUIRE(listed.has_value());
    REQUIRE(listed->message.has_value());
    CHECK(listed->message->find("Build Notes") != std::string::npos);

    auto found = codeharness::execute_slash_command(commands, "/memory search cmake build");
    REQUIRE(found.has_value());
    REQUIRE(found->message.has_value());
    CHECK(found->message->find("Matching memories:") != std::string::npos);
    CHECK(found->message->find("Build Notes") != std::string::npos);

    auto removed = codeharness::execute_slash_command(commands, "/memory remove Build Notes");
    REQUIRE(removed.has_value());
    REQUIRE(removed->message.has_value());
    CHECK(removed->message == "Removed memory: Build Notes\n");

    listed = codeharness::execute_slash_command(commands, "/memory list");
    REQUIRE(listed.has_value());
    REQUIRE(listed->message.has_value());
    CHECK(*listed->message == "No memories.\n");
}

TEST_CASE("CLI memory helper injects relevant memories into system prompt")
{
    TempDir temp{"codeharness-memory-prompt-injection-test"};
    codeharness::memory::MemoryStore store{temp.path / "memory"};
    REQUIRE(store
                .add(
                    codeharness::memory::AddMemoryRequest{
                        .title = "Build Notes",
                        .body = "Use CMake when building CodeHarness.",
                    })
                .has_value());

    auto memories = codeharness::load_relevant_memories_for_prompt(store, "How should I build with CMake?");
    REQUIRE(memories.has_value());
    REQUIRE(memories->size() == 1);

    codeharness::PromptBuildRequest request;
    request.cwd = temp.path;
    request.latest_user_prompt = "How should I build with CMake?";
    request.relevant_memories = std::move(*memories);

    auto prompt = codeharness::SystemPromptBuilder{}.build(request);

    REQUIRE(prompt.has_value());
    CHECK(prompt->find("# Relevant Memories") != std::string::npos);
    CHECK(prompt->find("## Build Notes") != std::string::npos);
    CHECK(prompt->find("Use CMake when building CodeHarness.") != std::string::npos);
}

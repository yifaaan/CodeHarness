#include "test_support.h"

#include "codeharness/mailbox/team_lifecycle.h"

using namespace codeharness::mailbox;

namespace
{

auto make_test_member(std::string name = "worker1", std::string team = "my-team") -> TeamMember
{
    TeamMember member;
    member.agent_id = name + "@" + team;
    member.name = std::move(name);
    member.backend_type = "subprocess";
    member.joined_at = "2026-06-04T10:00:00Z";
    return member;
}

} // namespace

TEST_CASE("TeamMember serializes and deserializes through JSON")
{
    auto original = make_test_member("coordinator", "test-team");
    original.joined_at = "2026-06-04T12:00:00Z";

    // 序列化：TeamMember → nlohmann::json
    const nlohmann::json json = original;

    // 验证序列化后的 JSON 结构
    CHECK(json.at("agent_id") == "coordinator@test-team");
    CHECK(json.at("name") == "coordinator");
    CHECK(json.at("backend_type") == "subprocess");
    CHECK(json.at("joined_at") == "2026-06-04T12:00:00Z");

    // 反序列化：nlohmann::json → TeamMember
    auto parsed = json.get<TeamMember>();
    CHECK(parsed.agent_id == original.agent_id);
    CHECK(parsed.name == original.name);
    CHECK(parsed.backend_type == original.backend_type);
    CHECK(parsed.joined_at == original.joined_at);
}

TEST_CASE("TeamFile serializes and deserializes with members")
{
    TeamFile original;
    original.name = "research-team";
    original.description = "A team for code analysis";
    original.created_at = "2026-06-04T08:00:00Z";
    original.lead_agent_id = "lead@research-team";
    original.members["lead@research-team"] = make_test_member("lead", "research-team");
    original.members["worker@research-team"] = make_test_member("worker", "research-team");

    const nlohmann::json json = original;

    // 验证顶层字段
    CHECK(json.at("name") == "research-team");
    CHECK(json.at("description") == "A team for code analysis");
    CHECK(json.at("lead_agent_id") == "lead@research-team");

    // 验证 members 是嵌套对象
    CHECK(json.at("members").is_object());
    CHECK(json.at("members").contains("lead@research-team"));
    CHECK(json.at("members").contains("worker@research-team"));

    // 往返测试：反序列化后应该和原始数据一致
    auto parsed = json.get<TeamFile>();
    CHECK(parsed.name == original.name);
    CHECK(parsed.description == original.description);
    CHECK(parsed.created_at == original.created_at);
    CHECK(parsed.lead_agent_id == original.lead_agent_id);
    CHECK(parsed.members.size() == 2);
    CHECK(parsed.members.contains("lead@research-team"));
    CHECK(parsed.members.contains("worker@research-team"));
}

TEST_CASE("TeamFile deserialization handles missing optional fields")
{
    // 只有 name 和 created_at 的最小 JSON（description 和 lead_agent_id 缺失）
    const auto json = nlohmann::json{
        {"name", "minimal-team"},
        {"created_at", "2026-06-04T00:00:00Z"},
    };

    auto parsed = json.get<TeamFile>();
    CHECK(parsed.name == "minimal-team");
    CHECK(parsed.description == "");          // 默认为空字符串
    CHECK(parsed.lead_agent_id == "");        // 默认为空字符串
    CHECK(parsed.members.empty());            // 默认为空 map
}

TEST_CASE("TeamFile deserialization skips corrupted members")
{
    // 一个成员正常，另一个成员缺少必需字段
    const auto json = nlohmann::json{
        {"name", "mixed-team"},
        {"created_at", "2026-06-04T00:00:00Z"},
        {"members",
         nlohmann::json{
             {"good@team",
              nlohmann::json{
                  {"agent_id", "good@team"},
                  {"name", "good"},
                  {"backend_type", "subprocess"},
                  {"joined_at", "2026-06-04T00:00:00Z"},
              }},
             {"bad@team",
              nlohmann::json{
                  {"agent_id", "bad@team"},
                  // 缺少 name 和 backend_type——会导致解析失败
              }},
         }},
    };

    auto parsed = json.get<TeamFile>();
    CHECK(parsed.name == "mixed-team");
    // 只有 good@team 被成功解析，bad@team 被跳过
    CHECK(parsed.members.size() == 1);
    CHECK(parsed.members.contains("good@team"));
    CHECK(!parsed.members.contains("bad@team"));
}

TEST_CASE("sanitize_team_name replaces non-alphanumeric chars and lowercases")
{
    CHECK(sanitize_team_name("My Team") == "my-team");
    CHECK(sanitize_team_name("worker@v2") == "worker-v2");
    CHECK(sanitize_team_name("test/../../etc") == "test-------etc");
    CHECK(sanitize_team_name("UPPER_CASE") == "upper-case");
    CHECK(sanitize_team_name("123abc") == "123abc");
    CHECK(sanitize_team_name("---") == "---");
    CHECK(sanitize_team_name("") == "");
}

TEST_CASE("is_valid_team_name rejects invalid names")
{
    // 空字符串
    CHECK(!is_valid_team_name(""));

    // 路径遍历
    CHECK(!is_valid_team_name("."));
    CHECK(!is_valid_team_name(".."));

    // 包含路径分隔符
    CHECK(!is_valid_team_name("foo/bar"));
    CHECK(!is_valid_team_name("foo\\bar"));

    // 纯符号（没有字母或数字）
    CHECK(!is_valid_team_name("---"));
    CHECK(!is_valid_team_name("!!!"));
}

TEST_CASE("is_valid_team_name accepts valid names")
{
    CHECK(is_valid_team_name("my-team"));
    CHECK(is_valid_team_name("team123"));
    CHECK(is_valid_team_name("A"));
    CHECK(is_valid_team_name("research-team-v2"));
    CHECK(is_valid_team_name("My Team"));       // 包含空格但有空格不意味着非法
    CHECK(is_valid_team_name("worker@team"));   // 包含 @ 但也包含字母
}

TEST_CASE("create_team creates team.json on disk")
{
    TempDir temp{"codeharness-team-create-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto team = mgr.create_team("research-team", "A team for code analysis");
    REQUIRE(team.has_value());

    // 验证返回的 TeamFile 字段
    CHECK(team->name == "research-team");
    CHECK(team->description == "A team for code analysis");
    CHECK(!team->created_at.empty());         // 应该自动填充时间戳
    CHECK(team->lead_agent_id.empty());       // 创建时不指定 leader
    CHECK(team->members.empty());             // 创建时没有成员

    // 验证磁盘上的文件存在
    CHECK(std::filesystem::exists(temp.path / "teams" / "research-team" / "team.json"));

    // 验证文件内容是合法 JSON
    auto content = read_file_text(temp.path / "teams" / "research-team" / "team.json");
    auto json = nlohmann::json::parse(content);
    CHECK(json.at("name") == "research-team");
    CHECK(json.at("description") == "A team for code analysis");
}

TEST_CASE("create_team rejects invalid team name")
{
    TempDir temp{"codeharness-team-invalid-name-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    CHECK(!mgr.create_team("").has_value());
    CHECK(!mgr.create_team(".").has_value());
    CHECK(!mgr.create_team("..").has_value());
    CHECK(!mgr.create_team("---").has_value());
}

TEST_CASE("create_team rejects duplicate team name")
{
    TempDir temp{"codeharness-team-duplicate-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("my-team").has_value());

    auto duplicate = mgr.create_team("my-team");
    CHECK(!duplicate.has_value());
    CHECK(duplicate.error().kind == codeharness::ErrorKind::AlreadyExists);
}

TEST_CASE("create_team uses atomic write with no leftover tmp files")
{
    TempDir temp{"codeharness-team-atomic-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("atomic-team").has_value());

    // 验证没有残留的 .tmp 文件
    auto team_dir = temp.path / "teams" / "atomic-team";
    for (const auto& entry : std::filesystem::directory_iterator{team_dir})
    {
        CHECK(entry.path().extension() != ".tmp");
    }
}

TEST_CASE("delete_team removes team directory")
{
    TempDir temp{"codeharness-team-delete-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("to-delete").has_value());
    CHECK(std::filesystem::exists(temp.path / "teams" / "to-delete" / "team.json"));

    auto result = mgr.delete_team("to-delete");
    REQUIRE(result.has_value());
    CHECK(!std::filesystem::exists(temp.path / "teams" / "to-delete"));
}

TEST_CASE("delete_team returns error for nonexistent team")
{
    TempDir temp{"codeharness-team-delete-missing-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.delete_team("nonexistent");
    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::NotFound);
}

TEST_CASE("get_team returns team data for existing team")
{
    TempDir temp{"codeharness-team-get-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto created = mgr.create_team("fetch-team", "Test description");
    REQUIRE(created.has_value());

    auto fetched = mgr.get_team("fetch-team");
    REQUIRE(fetched.has_value());
    REQUIRE(fetched->has_value());

    CHECK(fetched->value().name == "fetch-team");
    CHECK(fetched->value().description == "Test description");
    CHECK(fetched->value().created_at == created->created_at);
}

TEST_CASE("get_team returns nullopt for nonexistent team")
{
    TempDir temp{"codeharness-team-get-missing-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.get_team("nonexistent");
    REQUIRE(result.has_value());
    CHECK(!result->has_value()); // nullopt（不是错误）
}

TEST_CASE("list_teams returns all teams sorted by name")
{
    TempDir temp{"codeharness-team-list-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("charlie-team").has_value());
    REQUIRE(mgr.create_team("alpha-team").has_value());
    REQUIRE(mgr.create_team("bravo-team").has_value());

    auto teams = mgr.list_teams();
    REQUIRE(teams.has_value());
    REQUIRE(teams->size() == 3);

    // 验证按名称排序
    CHECK((*teams)[0].name == "alpha-team");
    CHECK((*teams)[1].name == "bravo-team");
    CHECK((*teams)[2].name == "charlie-team");
}

TEST_CASE("list_teams returns empty when no teams exist")
{
    TempDir temp{"codeharness-team-list-empty-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto teams = mgr.list_teams();
    REQUIRE(teams.has_value());
    CHECK(teams->empty());
}

TEST_CASE("list_teams skips directories without team.json")
{
    TempDir temp{"codeharness-team-list-skip-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    // 创建一个正常的团队
    REQUIRE(mgr.create_team("valid-team").has_value());

    // 创建一个没有 team.json 的目录（模拟残留或临时目录）
    std::filesystem::create_directories(temp.path / "teams" / "empty-dir");

    // 创建一个有损坏 team.json 的目录
    auto corrupt_dir = temp.path / "teams" / "corrupt-team";
    std::filesystem::create_directories(corrupt_dir);
    {
        std::ofstream file{corrupt_dir / "team.json", std::ios::binary};
        file << "this is not valid json";
    }

    auto teams = mgr.list_teams();
    REQUIRE(teams.has_value());
    REQUIRE(teams->size() == 1);
    CHECK(teams->front().name == "valid-team");
}

TEST_CASE("team data survives process restart (re-read from disk)")
{
    TempDir temp{"codeharness-team-restart-test"};

    // 第一个 TeamLifecycleManager 实例创建团队并添加成员
    {
        TeamLifecycleManager mgr{temp.path / "teams"};
        auto team = mgr.create_team("persistent-team", "Survives restart");
        REQUIRE(team.has_value());

        auto updated = mgr.add_member("persistent-team", make_test_member("worker1", "persistent-team"));
        REQUIRE(updated.has_value());
    }

    // 第二个 TeamLifecycleManager 实例（模拟进程重启后重新创建管理器）
    {
        TeamLifecycleManager mgr{temp.path / "teams"};

        auto team = mgr.get_team("persistent-team");
        REQUIRE(team.has_value());
        REQUIRE(team->has_value());

        CHECK(team->value().name == "persistent-team");
        CHECK(team->value().description == "Survives restart");
        CHECK(team->value().members.size() == 1);
        CHECK(team->value().members.contains("worker1@persistent-team"));
    }
}

TEST_CASE("add_member adds a member to a team")
{
    TempDir temp{"codeharness-team-addmember-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-with-members").has_value());

    auto member = make_test_member("researcher", "team-with-members");
    auto result = mgr.add_member("team-with-members", std::move(member));
    REQUIRE(result.has_value());

    CHECK(result->members.size() == 1);
    CHECK(result->members.contains("researcher@team-with-members"));

    // 验证成员字段
    const auto& m = result->members.at("researcher@team-with-members");
    CHECK(m.agent_id == "researcher@team-with-members");
    CHECK(m.name == "researcher");
    CHECK(m.backend_type == "subprocess");
}

TEST_CASE("add_member auto-fills joined_at if empty")
{
    TempDir temp{"codeharness-team-joined-at-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-auto-joined").has_value());

    TeamMember member;
    member.agent_id = "worker@team-auto-joined";
    member.name = "worker";
    member.backend_type = "subprocess";
    // joined_at 保持为空

    auto result = mgr.add_member("team-auto-joined", std::move(member));
    REQUIRE(result.has_value());

    const auto& m = result->members.at("worker@team-auto-joined");
    CHECK(!m.joined_at.empty()); // 应该被自动填充
}

TEST_CASE("add_member replaces existing member with same agent_id")
{
    TempDir temp{"codeharness-team-replace-member-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-replace").has_value());

    // 先添加一个成员
    auto member1 = make_test_member("worker", "team-replace");
    member1.backend_type = "subprocess";
    REQUIRE(mgr.add_member("team-replace", std::move(member1)).has_value());

    // 用相同的 agent_id 但不同的 backend_type 再次添加
    auto member2 = make_test_member("worker", "team-replace");
    member2.backend_type = "in_process";
    auto result = mgr.add_member("team-replace", std::move(member2));
    REQUIRE(result.has_value());

    // 应该只有一个成员，且 backend_type 被更新
    CHECK(result->members.size() == 1);
    CHECK(result->members.at("worker@team-replace").backend_type == "in_process");
}

TEST_CASE("add_member returns error for nonexistent team")
{
    TempDir temp{"codeharness-team-addmember-missing-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.add_member("nonexistent-team", make_test_member());
    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::NotFound);
}

TEST_CASE("remove_member removes a member from a team")
{
    TempDir temp{"codeharness-team-rmmember-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-remove-member").has_value());
    REQUIRE(mgr.add_member("team-remove-member", make_test_member("worker1", "team-remove-member")).has_value());
    REQUIRE(mgr.add_member("team-remove-member", make_test_member("worker2", "team-remove-member")).has_value());

    auto result = mgr.remove_member("team-remove-member", "worker1@team-remove-member");
    REQUIRE(result.has_value());

    CHECK(result->members.size() == 1);
    CHECK(!result->members.contains("worker1@team-remove-member"));
    CHECK(result->members.contains("worker2@team-remove-member"));
}

TEST_CASE("remove_member returns error for nonexistent member")
{
    TempDir temp{"codeharness-team-rmmember-missing-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-remove-missing").has_value());

    auto result = mgr.remove_member("team-remove-missing", "ghost@team-remove-missing");
    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::NotFound);
}

TEST_CASE("remove_member returns error for nonexistent team")
{
    TempDir temp{"codeharness-team-rmmember-noteam-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.remove_member("nonexistent-team", "someone@nonexistent-team");
    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::NotFound);
}

TEST_CASE("set_lead_agent sets the team leader")
{
    TempDir temp{"codeharness-team-leader-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-with-leader").has_value());

    auto result = mgr.set_lead_agent("team-with-leader", "coordinator@team-with-leader");
    REQUIRE(result.has_value());
    CHECK(result->lead_agent_id == "coordinator@team-with-leader");
}

TEST_CASE("set_lead_agent persists to disk")
{
    TempDir temp{"codeharness-team-leader-persist-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    REQUIRE(mgr.create_team("team-persist-leader").has_value());
    REQUIRE(mgr.set_lead_agent("team-persist-leader", "lead@team-persist-leader").has_value());

    // 重新读取验证持久化
    auto team = mgr.get_team("team-persist-leader");
    REQUIRE(team.has_value());
    REQUIRE(team->has_value());
    CHECK(team->value().lead_agent_id == "lead@team-persist-leader");
}

TEST_CASE("set_lead_agent returns error for nonexistent team")
{
    TempDir temp{"codeharness-team-leader-missing-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.set_lead_agent("nonexistent-team", "lead@nonexistent-team");
    CHECK(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::NotFound);
}

TEST_CASE("get_team handles corrupted team.json")
{
    TempDir temp{"codeharness-team-corrupt-test"};

    // 创建一个损坏的 team.json
    auto corrupt_dir = temp.path / "teams" / "corrupt-team";
    std::filesystem::create_directories(corrupt_dir);
    {
        std::ofstream file{corrupt_dir / "team.json", std::ios::binary};
        file << "this is not valid json";
    }

    TeamLifecycleManager mgr{temp.path / "teams"};

    auto result = mgr.get_team("corrupt-team");
    CHECK(!result.has_value()); // 应该返回错误（IO 错误）
}

TEST_CASE("team_dir returns correct path")
{
    TempDir temp{"codeharness-team-dir-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    CHECK(mgr.team_dir("my-team") == temp.path / "teams" / "my-team");
    CHECK(mgr.root() == temp.path / "teams");
}

TEST_CASE("full workflow: create team, add members, set leader, delete")
{
    // 这是一个端到端的综合测试，模拟完整的多 Agent 团队生命周期：
    //   1. 创建团队
    //   2. 设置 leader（Coordinator Agent）
    //   3. 添加两个 Worker Agent 作为成员
    //   4. 验证所有成员都在
    //   5. 移除一个 Worker
    //   6. 删除整个团队

    TempDir temp{"codeharness-team-workflow-test"};
    TeamLifecycleManager mgr{temp.path / "teams"};

    // 步骤 1：创建团队
    auto team = mgr.create_team("swarm-team", "A team for distributed analysis");
    REQUIRE(team.has_value());
    CHECK(team->name == "swarm-team");
    CHECK(team->members.empty());

    // 步骤 2：设置 leader
    auto with_leader = mgr.set_lead_agent("swarm-team", "coordinator@swarm-team");
    REQUIRE(with_leader.has_value());
    CHECK(with_leader->lead_agent_id == "coordinator@swarm-team");

    // 步骤 3：添加成员
    REQUIRE(mgr.add_member("swarm-team", make_test_member("coordinator", "swarm-team")).has_value());
    REQUIRE(mgr.add_member("swarm-team", make_test_member("researcher", "swarm-team")).has_value());
    REQUIRE(mgr.add_member("swarm-team", make_test_member("tester", "swarm-team")).has_value());

    // 步骤 4：验证
    auto current = mgr.get_team("swarm-team");
    REQUIRE(current.has_value());
    REQUIRE(current->has_value());
    CHECK(current->value().members.size() == 3);
    CHECK(current->value().lead_agent_id == "coordinator@swarm-team");

    // 步骤 5：移除一个成员
    auto after_remove = mgr.remove_member("swarm-team", "tester@swarm-team");
    REQUIRE(after_remove.has_value());
    CHECK(after_remove->members.size() == 2);
    CHECK(!after_remove->members.contains("tester@swarm-team"));

    // 步骤 6：删除团队
    REQUIRE(mgr.delete_team("swarm-team").has_value());
    auto gone = mgr.get_team("swarm-team");
    REQUIRE(gone.has_value());
    CHECK(!gone->has_value()); // 团队已不存在
}

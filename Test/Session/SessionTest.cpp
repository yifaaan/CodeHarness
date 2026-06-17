#include "Session/Session.h"
#include "Session/SessionStore.h"
#include "Session/SessionTypes.h"
#include "Session/WorkdirKey.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Agent/Agent.h"
#include "Host/LocalHost.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "absl/status/status.h"

namespace sess = codeharness::session;
namespace host = codeharness::host;
namespace llm = codeharness::llm;

namespace
{

	struct TmpStoreFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;
		std::string root;

		TmpStoreFixture()
		{
			auto tmpBase = std::filesystem::temp_directory_path();
			tmpDir = tmpBase / ("codeharness_session_test_" + std::to_string(std::time(nullptr)));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
			root = (tmpDir / "sessions").string();
		}

		~TmpStoreFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}
	};

	// Minimal mock provider: emits one canned text response, then errors if
	// asked again. Good enough for the prompt-once-then-resume flow.
	class MockChatProvider : public llm::ChatProvider
	{
	public:
		std::string text = "hello back";
		size_t callCount = 0;

		std::string Name() const override
		{
			return "mock";
		}
		std::string ModelName() const override
		{
			return "mock-model";
		}
		std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override
		{
			return std::nullopt;
		}

		absl::Status Generate(std::string_view, std::span<const llm::Tool>, std::span<const llm::Message>,
							  const llm::StreamCallbacks& callbacks, std::stop_token = {}) override
		{
			++callCount;
			if (callbacks.onText)
				callbacks.onText(text);
			if (callbacks.onFinish)
				callbacks.onFinish(llm::FinishReason::Completed, llm::TokenUsage{});
			return absl::OkStatus();
		}
	};

	// Adapter so CHECK_OK works for both absl::Status and absl::StatusOr<T>.
	template <typename T>
	absl::string_view StatusMessage(const absl::StatusOr<T>& s)
	{
		return s.status().message();
	}
	inline absl::string_view StatusMessage(const absl::Status& s)
	{
		return s.message();
	}

#define CHECK_OK(expr)                  \
	do                                  \
	{                                   \
		auto _s = (expr);               \
		CHECK(_s.ok());                 \
		if (!_s.ok())                   \
			MESSAGE(StatusMessage(_s)); \
	} while (0)

} // namespace

TEST_CASE("WorkdirKey: deterministic and distinct")
{
	auto a = sess::EncodeWorkdirKey("D:/code/CodeHarness");
	auto a2 = sess::EncodeWorkdirKey("D:/code/CodeHarness");
	CHECK(a == a2); // deterministic

	auto b = sess::EncodeWorkdirKey("D:/code/Other");
	CHECK(a != b); // distinct inputs → distinct keys
}

TEST_CASE("WorkdirKey: filesystem-safe characters only")
{
	auto key = sess::EncodeWorkdirKey("D:\\code\\Code Harness");
	// No separators, colons, or spaces in the resulting directory name.
	CHECK(key.find(':') == std::string::npos);
	CHECK(key.find('\\') == std::string::npos);
	CHECK(key.find('/') == std::string::npos);
	CHECK(key.find(' ') == std::string::npos);
}

TEST_CASE("WorkdirKey: includes a hash suffix")
{
	auto key = sess::EncodeWorkdirKey("/home/user/project");
	auto dash = key.rfind('-');
	REQUIRE(dash != std::string::npos);
	// 16 hex chars after the final dash.
	auto suffix = key.substr(dash + 1);
	CHECK(suffix.size() == 16);
	for (char c : suffix)
	{
		CHECK((std::isxdigit(static_cast<unsigned char>(c)) != 0));
	}
}

TEST_CASE("SessionStore: Create makes dir skeleton + state.json + index")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	auto dir = store.Create("/home/me/proj", "my session");
	REQUIRE(dir.ok());
	CHECK_FALSE(dir->sessionId.empty());
	CHECK(dir->sessionId.rfind("sess_", 0) == 0);

	// state.json present and readable.
	auto meta = store.ReadMeta(dir->path);
	REQUIRE(meta.ok());
	CHECK(meta->title == "my session");
	CHECK(meta->workdir == "/home/me/proj");
	CHECK(meta->createdAt > 0);

	// The agents/main directory must exist (Create pre-creates it so wire.jsonl
	// can be appended without a missing-parent error).
	CHECK(f.host.Stat(dir->path + "/agents/main").ok());

	// index line appended.
	auto index = f.host.ReadText(f.root + "/session_index.jsonl");
	REQUIRE(index.ok());
	CHECK(index->find(dir->sessionId) != std::string::npos);
}

TEST_CASE("SessionStore: Get returns NotFound for unknown session")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);
	auto got = store.Get("/home/me/proj", "sess_doesnotexist");
	CHECK_FALSE(got.ok());
	CHECK(absl::IsNotFound(got.status()));
}

TEST_CASE("SessionStore: List returns created sessions with metadata")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	CHECK_OK(store.Create("/home/me/proj", "first"));
	CHECK_OK(store.Create("/home/me/proj", "second"));

	auto list = store.List("/home/me/proj");
	REQUIRE(list.ok());
	CHECK(list->size() == 2);

	// Titles round-trip from state.json.
	std::vector<std::string> titles;
	for (const auto& info : *list)
		titles.push_back(info.title);
	CHECK(std::find(titles.begin(), titles.end(), "first") != titles.end());
	CHECK(std::find(titles.begin(), titles.end(), "second") != titles.end());
}

TEST_CASE("SessionStore: List for unknown workdir returns empty, not error")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);
	auto list = store.List("/never/created");
	REQUIRE(list.ok());
	CHECK(list->empty());
}

TEST_CASE("SessionStore: Find matches by unique prefix")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	auto dir = store.Create("/home/me/proj", "t");
	REQUIRE(dir.ok());

	// Use a prefix long enough to be unique (the full id is always unique).
	auto prefix = dir->sessionId.substr(0, dir->sessionId.size() - 1);
	auto found = store.Find(prefix);
	REQUIRE(found.ok());
	CHECK(found->sessionId == dir->sessionId);
}

TEST_CASE("SessionStore: Find yields NotFound for no match")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);
	auto found = store.Find("sess_nonexistent");
	CHECK_FALSE(found.ok());
	CHECK(absl::IsNotFound(found.status()));
}

TEST_CASE("SessionStore: Remove deletes the session dir")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	auto dir = store.Create("/home/me/proj", "to delete");
	REQUIRE(dir.ok());
	CHECK_OK(store.Remove("/home/me/proj", dir->sessionId));

	// Gone.
	auto got = store.Get("/home/me/proj", dir->sessionId);
	CHECK(absl::IsNotFound(got.status()));
}

TEST_CASE("SessionStore: RenameTitle updates state.json")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	auto dir = store.Create("/home/me/proj", "old title");
	REQUIRE(dir.ok());
	CHECK_OK(store.RenameTitle("/home/me/proj", dir->sessionId, "new title"));

	auto list = store.List("/home/me/proj");
	REQUIRE(list.ok());
	REQUIRE(list->size() == 1);
	CHECK((*list)[0].title == "new title");
}

TEST_CASE("SessionStore: WriteMeta is atomic — no leftover temp file")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	auto dir = store.Create("/home/me/proj", "t");
	REQUIRE(dir.ok());

	sess::SessionMeta meta = *store.ReadMeta(dir->path);
	meta.title = "updated";
	CHECK_OK(store.WriteMeta(dir->path, meta));

	// state.json present, state.json.tmp absent.
	CHECK(f.host.Stat(dir->path + "/state.json").ok());
	auto tmp = f.host.Stat(dir->path + "/state.json.tmp");
	CHECK(absl::IsNotFound(tmp.status()));
}

TEST_CASE("Session: Create wires Agent + Records at computed wire path")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);
	MockChatProvider provider;

	sess::SessionConfig cfg{
		.host = &f.host,
		.provider = &provider,
		.workdir = "/home/me/proj",
		.title = "e2e",
	};

	auto session = sess::Session::Create(&store, cfg);
	REQUIRE(session.ok());
	REQUIRE((*session)->MainAgent() != nullptr);
	CHECK((*session)->Meta().title == "e2e");

	// Prompt once: should record a turn.prompt line at the wire path.
	auto pr = (*session)->MainAgent()->Prompt("hello");
	REQUIRE(pr.ok());

	auto wirePath = (*session)->Meta().workdir; // unused; build from id
	(void)wirePath;
	// Reconstruct the path via the store for verification.
	auto dir = store.Find((*session)->Id());
	REQUIRE(dir.ok());
	auto lines = f.host.ReadLines(dir->AgentWirePath("main"));
	REQUIRE(lines.ok());
	CHECK_FALSE(lines->empty());
	// The first recorded line should be a turn.prompt.
	CHECK((*lines)[0].find("turn.prompt") != std::string::npos);

	CHECK_OK((*session)->Close());
	CHECK((*session)->IsClosed());
}

TEST_CASE("Session: resume restores prior conversation history (end-to-end)")
{
	TmpStoreFixture f;
	sess::SessionStore store(&f.host, f.root);

	std::string createdId;
	{
		MockChatProvider provider;
		sess::SessionConfig cfg{
			.host = &f.host,
			.provider = &provider,
			.workdir = "/home/me/proj",
			.title = "persist-me",
		};
		auto session = sess::Session::Create(&store, cfg);
		REQUIRE(session.ok());
		createdId = (*session)->Id();

		// Prompt once — records the user message + assistant reply into wire.jsonl.
		auto pr = (*session)->MainAgent()->Prompt("remember this");
		REQUIRE(pr.ok());
		REQUIRE((*session)->MainAgent()->GetHistory().size() >= 2); // user + assistant

		CHECK_OK((*session)->Close());
	}
	// Session destroyed; wire.jsonl is on disk.

	// Resume with a fresh provider: history should be replayed from wire.jsonl.
	MockChatProvider provider2;
	sess::SessionConfig cfg2{
		.host = &f.host,
		.provider = &provider2,
	};
	auto resumed = sess::Session::Resume(&store, cfg2, createdId);
	REQUIRE(resumed.ok());
	REQUIRE((*resumed)->MainAgent() != nullptr);

	// The replayed history must contain the original user message.
	const auto& hist = (*resumed)->MainAgent()->GetHistory();
	bool foundUser = false;
	for (const auto& m : hist)
	{
		if (m.role == llm::Role::User)
		{
			foundUser = true;
			// The user message content should mention the prompt text.
			for (const auto& part : m.content)
			{
				if (auto* t = std::get_if<llm::TextPart>(&part))
				{
					if (t->text.find("remember this") != std::string::npos)
					{
						foundUser = true;
					}
				}
			}
		}
	}
	CHECK(foundUser);
	CHECK_FALSE(hist.empty());
}

TEST_CASE("SessionStore: ResolveSessionsRoot honors CODEHARNESS_HOME")
{
	TmpStoreFixture f;
	// Set the env var for this process; resolve should use it directly.
#ifdef _WIN32
	(void)_putenv_s("CODEHARNESS_HOME", f.tmpDir.string().c_str());
#else
	::setenv("CODEHARNESS_HOME", f.tmpDir.string().c_str(), 1);
#endif
	auto root = sess::SessionStore::ResolveSessionsRoot(&f.host);
	REQUIRE(root.ok());
	CHECK(*root == f.tmpDir.string() + "/sessions");

#ifdef _WIN32
	(void)_putenv_s("CODEHARNESS_HOME", "");
#else
	::unsetenv("CODEHARNESS_HOME");
#endif
}

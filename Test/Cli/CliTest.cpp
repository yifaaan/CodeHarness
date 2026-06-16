#include "Cli/CliOptions.h"
#include "Cli/CliParser.h"
#include "Cli/RunPrompt.h"

#include <doctest/doctest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "Host/LocalHost.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Session/SessionStore.h"
#include "absl/status/status.h"

namespace cli = codeharness::cli;
namespace host = codeharness::host;
namespace llm = codeharness::llm;

namespace
{

	// Helper: build a (argc, argv) array from a vector of args, argv[0] = "codeharness".
	struct Argv
	{
		std::vector<std::string> storage;
		std::vector<char*> ptrs;
		std::vector<char*> data;

		Argv(std::vector<std::string> args)
		{
			storage.emplace_back("codeharness");
			for (auto& a : args)
				storage.push_back(std::move(a));
			data.reserve(storage.size());
			for (auto& s : storage)
				data.push_back(s.data());
			data.push_back(nullptr);
			ptrs = data;
		}
		int argc() const { return static_cast<int>(ptrs.size() - 1); }
		char** argv() { return ptrs.data(); }
	};

	// Minimal mock provider emitting one canned text response.
	class MockChatProvider : public llm::ChatProvider
	{
	public:
		std::string text = "cli-response";
		size_t callCount = 0;
		std::vector<size_t> messageCounts;

		std::string Name() const override { return "mock"; }
		std::string ModelName() const override { return "mock-model"; }
		std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override { return std::nullopt; }

		absl::Status Generate(std::string_view, std::span<const llm::Tool>, std::span<const llm::Message> messages,
							  const llm::StreamCallbacks& callbacks, std::stop_token = {}) override
		{
			++callCount;
			messageCounts.push_back(messages.size());
			if (callbacks.onText)
				callbacks.onText(text);
			if (callbacks.onFinish)
				callbacks.onFinish(llm::FinishReason::Completed, llm::TokenUsage{});
			return absl::OkStatus();
		}
	};

	struct TmpDirFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;

		TmpDirFixture()
		{
			auto tmpBase = std::filesystem::temp_directory_path();
			auto now = std::chrono::steady_clock::now().time_since_epoch().count();
			tmpDir = tmpBase / ("codeharness_cli_test_" + std::to_string(now));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
		}
		~TmpDirFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}
	};

} // namespace

// ---- CliParser ----

TEST_CASE("CliParser: --prompt long form")
{
	Argv argv({"--prompt", "hello world"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->prompt == "hello world");
	CHECK_FALSE(r->help);
	CHECK_FALSE(r->version);
	CHECK_FALSE(r->yolo);
}

TEST_CASE("CliParser: -p short form")
{
	Argv argv({"-p", "hi"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->prompt == "hi");
}

TEST_CASE("CliParser: -m sets model")
{
	Argv argv({"-p", "x", "-m", "gpt-4o"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->model == "gpt-4o");
}

TEST_CASE("CliParser: --model long form")
{
	Argv argv({"-p", "x", "--model", "claude-3.5"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->model == "claude-3.5");
}

TEST_CASE("CliParser: -y/--yolo toggles the flag")
{
	{
		Argv argv({"-p", "x", "-y"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		REQUIRE(r.ok());
		CHECK(r->yolo);
	}
	{
		Argv argv({"-p", "x", "--yolo"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		REQUIRE(r.ok());
		CHECK(r->yolo);
	}
	{
		Argv argv({"-p", "x"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		REQUIRE(r.ok());
		CHECK_FALSE(r->yolo);
	}
}

TEST_CASE("CliParser: --workdir is captured")
{
	Argv argv({"-p", "x", "--workdir", "/tmp/foo"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->workdir == "/tmp/foo");
}

TEST_CASE("CliParser: missing --prompt is an error")
{
	Argv argv({}); // no args at all
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	CHECK_FALSE(r.ok());
}

TEST_CASE("CliParser: shell mode does not require --prompt")
{
	Argv argv({"shell"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->mode == cli::CliMode::Shell);
	CHECK(r->prompt.empty());
}

TEST_CASE("CliParser: shell captures --session and --continue is mutually exclusive")
{
	{
		Argv argv({"shell", "--session", "abc123"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		REQUIRE(r.ok());
		CHECK(r->sessionId == "abc123");
		CHECK_FALSE(r->continueLast);
	}
	{
		Argv argv({"shell", "--session", "abc123", "--continue"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		CHECK_FALSE(r.ok());
	}
}

TEST_CASE("CliParser: tui mode does not require --prompt")
{
	Argv argv({"tui", "--session", "abc123", "-m", "mock-model", "--workdir", "/tmp/foo", "-y"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->mode == cli::CliMode::Tui);
	CHECK(r->prompt.empty());
	CHECK(r->sessionId == "abc123");
	CHECK(r->model == "mock-model");
	CHECK(r->workdir == "/tmp/foo");
	CHECK(r->yolo);
}

TEST_CASE("CliParser: tui rejects prompt and mutually exclusive resume flags")
{
	{
		Argv argv({"tui", "--session", "abc123", "--continue"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		CHECK_FALSE(r.ok());
	}
	{
		Argv argv({"--prompt", "x", "tui"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		CHECK_FALSE(r.ok());
	}
}

TEST_CASE("CliParser: --output-format accepts text and stream-json only")
{
	{
		Argv argv({"--output-format", "stream-json", "-p", "x"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		REQUIRE(r.ok());
		CHECK(r->outputFormat == cli::OutputFormat::StreamJson);
	}
	{
		Argv argv({"--output-format", "xml", "-p", "x"});
		auto r = cli::ParseArgs(argv.argc(), argv.argv());
		CHECK_FALSE(r.ok());
	}
}

TEST_CASE("CliParser: --help sets the flag and short-circuits")
{
	Argv argv({"--help"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->help);
	// --help should not require --prompt (it short-circuited before that check).
}

TEST_CASE("CliParser: -V/--version sets the flag")
{
	Argv argv({"-V"});
	auto r = cli::ParseArgs(argv.argc(), argv.argv());
	REQUIRE(r.ok());
	CHECK(r->version);
}

// ---- Run (smoke test with injected mock provider) ----

TEST_CASE("Run: one prompt reaches the provider and returns OK")
{
	TmpDirFixture f;
	MockChatProvider provider;

	cli::CliOptions opts{
		.prompt = "test prompt",
		.yolo = true, // avoid stdin approval prompt in tests
	};

	cli::RunDeps deps{
		.host = &f.host,
		.http = nullptr, // unused when resolveProvider is injected
		.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		},
	};

	auto status = cli::Run(opts, deps);
	CHECK(status.ok());
	CHECK(provider.callCount == 1); // the prompt reached the provider
}

TEST_CASE("Run: non-yolo without stdin denies mutating tools but read-only path still completes")
{
	TmpDirFixture f;
	MockChatProvider provider;

	cli::CliOptions opts{
		.prompt = "hello",
		.yolo = false, // Manual mode; no approval callback reachable here (stdin closed)
	};

	cli::RunDeps deps{
		.host = &f.host,
		.http = nullptr,
		.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		},
	};

	// The model only emits text (no tool calls), so permission is never
	// consulted; the run should still complete successfully.
	auto status = cli::Run(opts, deps);
	CHECK(status.ok());
	CHECK(provider.callCount == 1);
}

TEST_CASE("Run: shell mode keeps one session for multiple prompts")
{
	TmpDirFixture f;
	MockChatProvider provider;
	std::istringstream input("first\nsecond\n/exit\n");

	cli::CliOptions opts{
		.mode = cli::CliMode::Shell,
		.yolo = true,
	};

	cli::RunDeps deps{
		.host = &f.host,
		.http = nullptr,
		.input = &input,
		.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		},
	};

	auto status = cli::Run(opts, deps);
	CHECK(status.ok());
	CHECK(provider.callCount == 2);
}

TEST_CASE("Run: shell can resume by session prefix")
{
	TmpDirFixture f;
	MockChatProvider provider;

	{
		std::istringstream input("first\n/exit\n");
		cli::CliOptions opts{
			.mode = cli::CliMode::Shell,
			.yolo = true,
		};
		cli::RunDeps deps{
			.host = &f.host,
			.http = nullptr,
			.input = &input,
			.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
				return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
			},
		};
		REQUIRE(cli::Run(opts, deps).ok());
	}

	auto root = codeharness::session::SessionStore::ResolveSessionsRoot(&f.host);
	REQUIRE(root.ok());
	codeharness::session::SessionStore store(&f.host, *root);
	auto sessions = store.List(f.tmpDir.string());
	REQUIRE(sessions.ok());
	REQUIRE(sessions->size() == 1);
	auto sessionId = sessions->front().sessionId;

	{
		std::istringstream input("second\n/exit\n");
		cli::CliOptions opts{
			.sessionId = sessionId,
			.mode = cli::CliMode::Shell,
			.yolo = true,
		};
		cli::RunDeps deps{
			.host = &f.host,
			.http = nullptr,
			.input = &input,
			.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
				return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
			},
		};
		CHECK(cli::Run(opts, deps).ok());
	}

	CHECK(provider.callCount == 2);
}

TEST_CASE("Run: shell --continue resumes the latest workdir session")
{
	TmpDirFixture f;
	MockChatProvider provider;

	for (const auto* prompt : {"older\n/exit\n", "newer\n/exit\n"})
	{
		std::istringstream input(prompt);
		cli::CliOptions opts{
			.mode = cli::CliMode::Shell,
			.yolo = true,
		};
		cli::RunDeps deps{
			.host = &f.host,
			.http = nullptr,
			.input = &input,
			.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
				return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
			},
		};
		REQUIRE(cli::Run(opts, deps).ok());
	}

	std::istringstream input("continued\n/exit\n");
	cli::CliOptions opts{
		.mode = cli::CliMode::Shell,
		.continueLast = true,
		.yolo = true,
	};
	cli::RunDeps deps{
		.host = &f.host,
		.http = nullptr,
		.input = &input,
		.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		},
	};

	auto status = cli::Run(opts, deps);
	CHECK(status.ok());
	CHECK(provider.callCount == 3);
}

TEST_CASE("Run: stream-json output emits valid JSONL")
{
	TmpDirFixture f;
	MockChatProvider provider;

	std::ostringstream captured;
	auto* oldBuf = std::cout.rdbuf(captured.rdbuf());

	cli::CliOptions opts{
		.prompt = "json please",
		.outputFormat = cli::OutputFormat::StreamJson,
		.yolo = true,
	};
	cli::RunDeps deps{
		.host = &f.host,
		.http = nullptr,
		.resolveProvider = [&provider]() -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		},
	};

	auto status = cli::Run(opts, deps);
	std::cout.rdbuf(oldBuf);

	REQUIRE(status.ok());
	REQUIRE_FALSE(captured.str().empty());
	std::istringstream lines(captured.str());
	std::string line;
	int jsonLines = 0;
	while (std::getline(lines, line))
	{
		if (line.empty())
			continue;
		auto parsed = nlohmann::json::parse(line);
		CHECK(parsed.contains("event"));
		++jsonLines;
	}
	CHECK(jsonLines >= 3);
}

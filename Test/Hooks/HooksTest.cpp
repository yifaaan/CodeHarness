#include "Hooks/HookEngine.h"
#include "Hooks/HookTypes.h"

#include <doctest/doctest.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Host/LocalHost.h"
#include "absl/status/status.h"

namespace hooks_ns = codeharness::hooks;
namespace host = codeharness::host;

namespace
{

	struct TmpScriptsFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;

		TmpScriptsFixture()
		{
			auto base = std::filesystem::temp_directory_path();
			tmpDir = base / ("codeharness_hooks_test_" + std::to_string(std::time(nullptr)));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
		}

		~TmpScriptsFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}

#ifdef _WIN32
		// Write a .cmd batch file that does `command`, returning its invocation argv.
		std::vector<std::string> WriteCmd(const std::string& name, const std::string& body)
		{
			auto path = tmpDir / (name + ".cmd");
			std::ofstream f(path, std::ios::binary);
			f << "@echo off\n"
			  << body << "\n";
			f.close();
			return {"cmd", "/c", path.string()};
		}
#else
		// Write a shell script, chmod +x, returning its invocation argv.
		std::vector<std::string> WriteSh(const std::string& name, const std::string& body)
		{
			auto path = tmpDir / (name + ".sh");
			std::ofstream f(path, std::ios::binary);
			f << "#!/bin/sh\n"
			  << body << "\n";
			f.close();
			std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
			return {path.string()};
		}
#endif

		// Build a hook whose command runs the given script. `args` is the argv
		// returned by WriteCmd/WriteSh; we join it into the space-split command string.
		hooks_ns::HookDef MakeHook(hooks_ns::HookEvent event, const std::vector<std::string>& args,
								   std::optional<std::string> matcher = std::nullopt)
		{
			hooks_ns::HookDef h;
			h.event = event;
			// Join argv with spaces (HookEngine re-splits on spaces). Paths with
			// spaces would break this, but tmp dir paths are space-free here.
			std::string cmd;
			for (const auto& a : args)
			{
				if (!cmd.empty())
					cmd += ' ';
				cmd += a;
			}
			h.command = cmd;
			h.matcher = std::move(matcher);
			h.timeoutSeconds = 10;
			return h;
		}
	};

} // namespace

TEST_CASE("HookEventName / ParseHookEvent round-trip")
{
	for (auto e : {
			 hooks_ns::HookEvent::PreToolUse,
			 hooks_ns::HookEvent::PostToolUse,
			 hooks_ns::HookEvent::PostToolUseFailure,
			 hooks_ns::HookEvent::UserPromptSubmit,
			 hooks_ns::HookEvent::Stop,
			 hooks_ns::HookEvent::StopFailure,
			 hooks_ns::HookEvent::SessionStart,
			 hooks_ns::HookEvent::SessionEnd,
			 hooks_ns::HookEvent::PreCompact,
			 hooks_ns::HookEvent::PostCompact,
			 hooks_ns::HookEvent::Notification,
		 })
	{
		auto name = hooks_ns::HookEventName(e);
		auto parsed = hooks_ns::ParseHookEvent(name);
		REQUIRE(parsed.has_value());
		CHECK(*parsed == e);
	}
	CHECK_FALSE(hooks_ns::ParseHookEvent("bogus").has_value());
}

TEST_CASE("HookEngine: empty engine Trigger is a no-op")
{
	TmpScriptsFixture f;
	hooks_ns::HookEngine engine({}, &f.host);
	CHECK(engine.Empty());

	auto results = engine.Trigger(hooks_ns::HookEvent::Stop, {hooks_ns::HookEvent::Stop, "", {}});
	CHECK(results.empty());

	auto block = engine.TriggerBlock(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Bash", {}});
	CHECK_FALSE(block.has_value());
}

TEST_CASE("HookEngine: matcher filters by target")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto args = f.WriteCmd("match", "exit /b 0");
#else
	auto args = f.WriteSh("match", "exit 0");
#endif
	auto hook = f.MakeHook(hooks_ns::HookEvent::PreToolUse, args, "Bash");
	hooks_ns::HookEngine engine({hook}, &f.host);

	// target "Bash" matches; target "Read" does not.
	auto results = engine.Trigger(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Read", {}});
	CHECK(results.empty()); // no match → no hook ran

	results = engine.Trigger(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Bash", {}});
	CHECK(results.size() == 1);
	CHECK(results[0].action == hooks_ns::HookAction::Allow);
}

TEST_CASE("HookEngine: fail-open on non-zero exit")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto args = f.WriteCmd("fail", "exit /b 1");
#else
	auto args = f.WriteSh("fail", "exit 1");
#endif
	auto hook = f.MakeHook(hooks_ns::HookEvent::PreToolUse, args);
	hooks_ns::HookEngine engine({hook}, &f.host);

	auto results = engine.Trigger(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Bash", {}});
	REQUIRE(results.size() == 1);
	CHECK(results[0].failed);
	CHECK(results[0].action == hooks_ns::HookAction::Allow); // fail-open
	CHECK(results[0].exitCode != 0);
}

TEST_CASE("HookEngine: explicit block via stdout JSON")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	// echo a JSON line; cmd's echo needs the quotes escaped carefully.
	auto args = f.WriteCmd("block", "echo {\"action\":\"block\",\"reason\":\"dangerous\"}");
#else
	auto args = f.WriteSh("block", "printf '%s\\n' '{\"action\":\"block\",\"reason\":\"dangerous\"}'");
#endif
	auto hook = f.MakeHook(hooks_ns::HookEvent::PreToolUse, args);
	hooks_ns::HookEngine engine({hook}, &f.host);

	auto block = engine.TriggerBlock(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Bash", {}});
	REQUIRE(block.has_value());
	CHECK(block->action == hooks_ns::HookAction::Block);
	CHECK(block->reason == "dangerous");
	CHECK_FALSE(block->failed);
}

TEST_CASE("HookEngine: TriggerBlock returns nullopt when all hooks allow")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto args = f.WriteCmd("allow", "exit /b 0");
#else
	auto args = f.WriteSh("allow", "exit 0");
#endif
	auto hook = f.MakeHook(hooks_ns::HookEvent::UserPromptSubmit, args);
	hooks_ns::HookEngine engine({hook}, &f.host);

	auto block = engine.TriggerBlock(hooks_ns::HookEvent::UserPromptSubmit, {hooks_ns::HookEvent::UserPromptSubmit, "", {}});
	CHECK_FALSE(block.has_value());
}

TEST_CASE("HookEngine: TriggerBlock returns first blocker among multiple hooks")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto allowArgs = f.WriteCmd("allow2", "exit /b 0");
	auto blockArgs = f.WriteCmd("block2", "echo {\"action\":\"block\",\"reason\":\"stop\"}");
#else
	auto allowArgs = f.WriteSh("allow2", "exit 0");
	auto blockArgs = f.WriteSh("block2", "printf '%s\\n' '{\"action\":\"block\",\"reason\":\"stop\"}'");
#endif
	auto allowHook = f.MakeHook(hooks_ns::HookEvent::PreToolUse, allowArgs);
	auto blockHook = f.MakeHook(hooks_ns::HookEvent::PreToolUse, blockArgs);
	hooks_ns::HookEngine engine({allowHook, blockHook}, &f.host);

	auto block = engine.TriggerBlock(hooks_ns::HookEvent::PreToolUse, {hooks_ns::HookEvent::PreToolUse, "Bash", {}});
	REQUIRE(block.has_value());
	CHECK(block->action == hooks_ns::HookAction::Block);
	CHECK(block->reason == "stop");
}

TEST_CASE("HookEngine: informational events run all matching hooks via Trigger")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto args1 = f.WriteCmd("stop1", "exit /b 0");
	auto args2 = f.WriteCmd("stop2", "exit /b 0");
#else
	auto args1 = f.WriteSh("stop1", "exit 0");
	auto args2 = f.WriteSh("stop2", "exit 0");
#endif
	auto h1 = f.MakeHook(hooks_ns::HookEvent::Stop, args1);
	auto h2 = f.MakeHook(hooks_ns::HookEvent::Stop, args2);
	hooks_ns::HookEngine engine({h1, h2}, &f.host);

	auto results = engine.Trigger(hooks_ns::HookEvent::Stop, {hooks_ns::HookEvent::Stop, "", {}});
	CHECK(results.size() == 2); // both ran
}

TEST_CASE("HookEngine: stdout is captured in HookResult")
{
	TmpScriptsFixture f;
#ifdef _WIN32
	auto args = f.WriteCmd("echo1", "echo hello-from-hook");
#else
	auto args = f.WriteSh("echo1", "printf '%s\\n' 'hello-from-hook'");
#endif
	auto hook = f.MakeHook(hooks_ns::HookEvent::PostToolUse, args);
	hooks_ns::HookEngine engine({hook}, &f.host);

	auto results = engine.Trigger(hooks_ns::HookEvent::PostToolUse, {hooks_ns::HookEvent::PostToolUse, "Read", {}});
	REQUIRE(results.size() == 1);
	CHECK(results[0].out.find("hello-from-hook") != std::string::npos);
	CHECK_FALSE(results[0].failed);
}

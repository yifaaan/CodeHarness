#include "Permission/PermissionGate.h"
#include "Permission/PermissionTypes.h"
#include "Config/ConfigTypes.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <string>

namespace perm = codeharness::permission;
namespace cfg = codeharness::config;
using json = nlohmann::json;

namespace
{

	// Counts how many times the callback was invoked and returns Allow or Deny.
	struct CountingCallback
	{
		int calls = 0;
		mutable std::string lastTool;
		mutable std::string lastDescription;
		perm::PermissionDecision decision = perm::PermissionDecision::Allow;

		perm::ApprovalCallback Make()
		{
			return [this](std::string_view toolName, const json&, std::string_view description) {
				++calls;
				lastTool = std::string(toolName);
				lastDescription = std::string(description);
				return decision;
			};
		}
	};

} // namespace

TEST_CASE("Permission: Yolo mode never invokes the callback and allows mutating tools")
{
	CountingCallback cb;
	perm::PermissionGate gate(cfg::PermissionMode::Yolo, cb.Make());

	CHECK(gate.ShouldRun(true, "write_file", json{}, "write file"));
	CHECK(gate.ShouldRun(true, "bash", json{}, "run shell"));
	CHECK(gate.ShouldRun(false, "read_file", json{}, "read file"));
	CHECK(cb.calls == 0); // Yolo short-circuits entirely
}

TEST_CASE("Permission: read-only tools are always allowed regardless of mode")
{
	CountingCallback cb;
	perm::PermissionGate gate(cfg::PermissionMode::Manual, cb.Make());

	CHECK(gate.ShouldRun(false, "read_file", json{}, "read"));
	CHECK(gate.ShouldRun(false, "glob", json{}, "glob"));
	CHECK(cb.calls == 0); // requiresPermission == false skips the callback
}

TEST_CASE("Permission: Manual mode invokes the callback for mutating tools")
{
	CountingCallback cb;
	cb.decision = perm::PermissionDecision::Allow;
	perm::PermissionGate gate(cfg::PermissionMode::Manual, cb.Make());

	CHECK(gate.ShouldRun(true, "write_file", json{{"path", "a.txt"}}, "write a.txt"));
	CHECK(cb.calls == 1);
	CHECK(cb.lastTool == "write_file");
	CHECK(cb.lastDescription == "write a.txt");
}

TEST_CASE("Permission: Manual mode denies when the callback returns Deny")
{
	CountingCallback cb;
	cb.decision = perm::PermissionDecision::Deny;
	perm::PermissionGate gate(cfg::PermissionMode::Manual, cb.Make());

	CHECK_FALSE(gate.ShouldRun(true, "bash", json{}, "run rm -rf"));
	CHECK(cb.calls == 1);
}

TEST_CASE("Permission: Manual mode with no callback denies mutating tools (safe default)")
{
	perm::PermissionGate gate(cfg::PermissionMode::Manual, {});

	// Mutating tool is denied — the safe default when no UI is wired.
	CHECK_FALSE(gate.ShouldRun(true, "write_file", json{}, "write"));
	// Read-only tools still pass.
	CHECK(gate.ShouldRun(false, "read_file", json{}, "read"));
}

TEST_CASE("Permission: Auto mode falls back to Manual behavior")
{
	CountingCallback cb;
	cb.decision = perm::PermissionDecision::Allow;
	perm::PermissionGate gate(cfg::PermissionMode::Auto, cb.Make());

	// Auto should behave like Manual (ask the callback) until session-scoped
	// state exists to implement true Auto semantics.
	CHECK(gate.ShouldRun(true, "write_file", json{}, "write"));
	CHECK(cb.calls == 1);
	CHECK(gate.Mode() == cfg::PermissionMode::Auto); // reported mode unchanged
}

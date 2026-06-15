#pragma once

#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "Hooks/HookTypes.h"

namespace codeharness::host
{
	class Host;
}

namespace codeharness::hooks
{

	// HookEngine runs user-configured subprocess hooks on agent lifecycle events.
	//
	// Fail-open invariant (Architecture Invariant #2): a hook command that fails
	// (non-zero exit, timeout, crash) is ALWAYS treated as Allow. The only way a
	// hook blocks is by printing a JSON line `{"action":"block","reason":"..."}`
	// on stdout, and only for the two blocking events (PreToolUse,
	// UserPromptSubmit). A broken script can never wedge or block the agent.
	//
	// The engine owns no per-session state beyond the hook list + host pointer.
	// All subprocess I/O goes through Host::ExecWithEnv + HostProcess, so it is
	// testable with LocalHost against tmp scripts and works on every platform.
	class HookEngine
	{
	public:
		HookEngine(std::vector<HookDef> hooks, host::Host* host);

		// Best-effort fan-out: run every hook subscribed to `event` whose matcher
		// matches `ctx.target`, return all results. Used for the 9 informational
		// events. Never blocks the caller's control flow.
		std::vector<HookResult> Trigger(HookEvent event, const HookContext& ctx, std::stop_token stopToken = {});

		// Blocking query for PreToolUse / UserPromptSubmit: run matching hooks in
		// order, return the first Block result, or nullopt if all allow. Hook
		// failures resolve to Allow (fail-open).
		std::optional<HookResult> TriggerBlock(HookEvent event, const HookContext& ctx, std::stop_token stopToken = {});

		bool Empty() const { return hooks.empty(); }

	private:
		// True if the hook's event matches and its matcher (if any) matches target.
		bool Matches(const HookDef& hook, HookEvent event, std::string_view target) const;

		// Spawn the hook command, pipe the JSON payload to its stdin, drain with
		// timeout, parse stdout for an optional block decision. Fail-open on any error.
		HookResult RunOne(const HookDef& hook, const HookContext& ctx, std::stop_token stopToken);

		std::vector<HookDef> hooks;
		host::Host* host;
	};

} // namespace codeharness::hooks

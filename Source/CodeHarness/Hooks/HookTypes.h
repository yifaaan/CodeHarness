#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace codeharness::hooks
{

	// Lifecycle events a hook can subscribe to. 11 of the 13 from the reference
	// design are implemented; SubagentStart/SubagentStop are deferred until the
	// subagent module exists.
	enum class HookEvent
	{
		PreToolUse,		  // blocking: before a tool runs
		PostToolUse,	  // after a successful tool result
		PostToolUseFailure, // after a tool error
		UserPromptSubmit,   // blocking: before the prompt reaches the agent
		Stop,				  // agent completed a turn normally
		StopFailure,		  // agent turn ended in error
		SessionStart,		  // session created/resumed
		SessionEnd,		  // session closing
		PreCompact,		  // before context compaction
		PostCompact,		  // after context compaction
		Notification,		  // general-purpose glue event
	};

	// Decision returned by a blocking hook (PreToolUse / UserPromptSubmit).
	// Informational events ignore this — they are fire-and-forget.
	enum class HookAction
	{
		Allow,
		Block,
	};

	// String forms used in config.toml and the JSON payload. Round-trip with
	// ParseHookEvent / HookEventName.
	std::string_view HookEventName(HookEvent e);
	std::optional<HookEvent> ParseHookEvent(std::string_view name);

	// One [[hooks]] entry from config.toml.
	struct HookDef
	{
		HookEvent event = HookEvent::Notification;
		std::string command;					   // shell command to execute
		std::optional<std::string> matcher; // regex against the event target (empty = match all)
		int timeoutSeconds = 30;			   // clamped to 1..600 at parse time
	};

	// Outcome of running a single hook command.
	struct HookResult
	{
		HookAction action = HookAction::Allow; // Allow unless an explicit block is returned
		std::string reason;					   // human-readable, from the hook's JSON or the failure
		std::string out;					   // captured stdout
		std::string err;					   // captured stderr
		int exitCode = 0;
		bool failed = false; // non-zero exit / timeout / crash (always resolves to Allow per fail-open)
	};

	// The context for one hook firing. `target` is what the matcher runs against
	// (tool name for PreToolUse/Post*, session id, etc.); `payload` carries the
	// event-specific fields serialized to JSON on the hook's stdin.
	struct HookContext
	{
		HookEvent event = HookEvent::Notification;
		std::string target;
		nlohmann::json payload;
	};

} // namespace codeharness::hooks

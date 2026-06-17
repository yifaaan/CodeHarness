#include "Hooks/HookEngine.h"

#include <algorithm>
#include <chrono>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Host/Host.h"
#include "Host/HostProcess.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace codeharness::hooks
{

	namespace
	{

		// Build the JSON payload piped to the hook's stdin. Always includes event
		// + target; merges the caller-supplied payload fields.
		std::string BuildStdinPayload(const HookContext& ctx)
		{
			nlohmann::json j;
			j["event"] = std::string(HookEventName(ctx.event));
			j["target"] = ctx.target;
			if (ctx.payload.is_object())
			{
				for (auto it = ctx.payload.begin(); it != ctx.payload.end(); ++it)
				{
					j[it.key()] = it.value();
				}
			}
			return j.dump();
		}

		// Parse an optional `{"action":"block","reason":"..."}` line from stdout.
		// The hook may print other text (logs); we scan for the first line that
		// parses as a JSON object with action == "block". Anything malformed → Allow.
		// (Parameter named `out` not `stdout` — the latter is a macro on MSVC.)
		HookAction ParseBlockDecision(const std::string& out, std::string& reason)
		{
			std::istringstream in(out);
			std::string line;
			while (std::getline(in, line))
			{
				// Trim leading/trailing whitespace.
				auto begin = line.find_first_not_of(" \t\r\n");
				if (begin == std::string::npos)
					continue;
				auto end = line.find_last_not_of(" \t\r\n");
				std::string trimmed = line.substr(begin, end - begin + 1);
				if (trimmed.empty() || trimmed.front() != '{')
					continue;
				try
				{
					auto obj = nlohmann::json::parse(trimmed);
					if (obj.is_object() && obj.value("action", "") == "block")
					{
						reason = obj.value("reason", "");
						return HookAction::Block;
					}
				}
				catch (const nlohmann::json::parse_error&)
				{
					// Not JSON or not a block — keep scanning.
				}
			}
			return HookAction::Allow;
		}

	} // namespace

	std::string_view HookEventName(HookEvent e)
	{
		switch (e)
		{
		case HookEvent::PreToolUse:
			return "PreToolUse";
		case HookEvent::PostToolUse:
			return "PostToolUse";
		case HookEvent::PostToolUseFailure:
			return "PostToolUseFailure";
		case HookEvent::UserPromptSubmit:
			return "UserPromptSubmit";
		case HookEvent::Stop:
			return "Stop";
		case HookEvent::StopFailure:
			return "StopFailure";
		case HookEvent::SessionStart:
			return "SessionStart";
		case HookEvent::SessionEnd:
			return "SessionEnd";
		case HookEvent::PreCompact:
			return "PreCompact";
		case HookEvent::PostCompact:
			return "PostCompact";
		case HookEvent::Notification:
			return "Notification";
		}
		return "Notification";
	}

	std::optional<HookEvent> ParseHookEvent(std::string_view name)
	{
		if (name == "PreToolUse")
			return HookEvent::PreToolUse;
		if (name == "PostToolUse")
			return HookEvent::PostToolUse;
		if (name == "PostToolUseFailure")
			return HookEvent::PostToolUseFailure;
		if (name == "UserPromptSubmit")
			return HookEvent::UserPromptSubmit;
		if (name == "Stop")
			return HookEvent::Stop;
		if (name == "StopFailure")
			return HookEvent::StopFailure;
		if (name == "SessionStart")
			return HookEvent::SessionStart;
		if (name == "SessionEnd")
			return HookEvent::SessionEnd;
		if (name == "PreCompact")
			return HookEvent::PreCompact;
		if (name == "PostCompact")
			return HookEvent::PostCompact;
		if (name == "Notification")
			return HookEvent::Notification;
		return std::nullopt;
	}

	HookEngine::HookEngine(std::vector<HookDef> hookList, host::Host* hostPtr)
		: hooks(std::move(hookList)), host(hostPtr)
	{
	}

	bool HookEngine::Matches(const HookDef& hook, HookEvent event, std::string_view target) const
	{
		if (hook.event != event)
			return false;
		if (!hook.matcher.has_value() || hook.matcher->empty())
			return true;
		try
		{
			std::regex re(*hook.matcher);
			return std::regex_search(std::string(target), re);
		}
		catch (const std::regex_error& e)
		{
			// A bad regex is a config error; treat as no-match rather than crash.
			spdlog::warn("hooks: bad matcher regex '{}': {}", *hook.matcher, e.what());
			return false;
		}
	}

	HookResult HookEngine::RunOne(const HookDef& hook, const HookContext& ctx, std::stop_token stopToken)
	{
		HookResult result;
		if (host == nullptr)
		{
			result.failed = true;
			result.reason = "no host available";
			return result; // fail-open (Allow)
		}

		// Split the command into argv for ExecWithEnv (no shell, no injection).
		// For MVP we honor a simple space-splitting; users wanting complex shell
		// constructs can wrap in `sh -c "..."` / `cmd /c "..."`.
		std::vector<std::string> args;
		{
			std::istringstream in(hook.command);
			std::string tok;
			while (in >> tok)
				args.push_back(tok);
		}
		if (args.empty())
		{
			result.failed = true;
			result.reason = "empty hook command";
			return result;
		}

		auto procStatus = host->ExecWithEnv(args, "", {});
		if (!procStatus.ok())
		{
			result.failed = true;
			result.reason = std::string(procStatus.status().message());
			return result;
		}
		auto& proc = **procStatus;

		// Pipe the JSON payload to stdin, then close to signal EOF.
		auto payload = BuildStdinPayload(ctx);
		if (auto s = proc.WriteStdin(payload); !s.ok())
			spdlog::warn("hooks: WriteStdin failed: {}", s.message());
		(void)proc.CloseStdin();

		int timeoutMs = std::clamp(hook.timeoutSeconds, 1, 600) * 1000;
		auto drain = proc.Drain(timeoutMs, stopToken);
		if (!drain.ok())
		{
			// Spawn/drain failure (not a timeout) → fail-open.
			result.failed = true;
			result.reason = std::string(drain.status().message());
			return result;
		}

		result.out = drain->out;
		result.err = drain->err;
		result.exitCode = drain->exitCode;

		bool timedOut = drain->timedOut;
		bool cancelled = !drain->finished && !drain->timedOut;
		if (cancelled || timedOut || drain->exitCode != 0)
		{
			// Fail-open: any non-success → Allow, mark failed.
			(void)proc.Kill("SIGTERM");
			result.failed = true;
			if (timedOut)
				result.reason = fmt::format("hook timed out after {}s", hook.timeoutSeconds);
			else if (cancelled)
				result.reason = "hook cancelled";
			else
				result.reason = fmt::format("hook exited with code {}", drain->exitCode);
			return result;
		}

		// Success: parse an optional explicit block decision from stdout.
		std::string reason;
		if (ParseBlockDecision(drain->out, reason) == HookAction::Block)
		{
			result.action = HookAction::Block;
			result.reason = std::move(reason);
		}
		return result;
	}

	std::vector<HookResult> HookEngine::Trigger(HookEvent event, const HookContext& ctx, std::stop_token stopToken)
	{
		std::vector<HookResult> results;
		if (hooks.empty())
			return results;
		for (const auto& hook : hooks)
		{
			if (!Matches(hook, event, ctx.target))
				continue;
			auto r = RunOne(hook, ctx, stopToken);
			if (!r.reason.empty() && r.failed)
				spdlog::debug("hooks: {} ({}) failed-open: {}", HookEventName(event), hook.command, r.reason);
			results.push_back(std::move(r));
		}
		return results;
	}

	std::optional<HookResult> HookEngine::TriggerBlock(HookEvent event, const HookContext& ctx, std::stop_token stopToken)
	{
		if (hooks.empty())
			return std::nullopt;
		for (const auto& hook : hooks)
		{
			if (!Matches(hook, event, ctx.target))
				continue;
			auto r = RunOne(hook, ctx, stopToken);
			if (r.action == HookAction::Block)
				return r;
			// Failed hooks (fail-open) and explicit Allows fall through to the next.
		}
		return std::nullopt;
	}

} // namespace codeharness::hooks

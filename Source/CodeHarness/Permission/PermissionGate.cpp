#include "Permission/PermissionGate.h"

#include <utility>

#include "spdlog/spdlog.h"

namespace codeharness::permission
{

	PermissionGate::PermissionGate(config::PermissionMode mode, ApprovalCallback callback)
		: mode(mode), callback(std::move(callback))
	{
	}

	bool PermissionGate::ShouldRun(bool requiresPermission, std::string_view toolName, const nlohmann::json& args, std::string_view description)
	{
		// Read-only tools never need approval regardless of mode. This preserves
		// the existing loop behavior for Read/Glob/Grep exactly.
		if (!requiresPermission)
		{
			return true;
		}

		config::PermissionMode effective = mode;

		// Auto mode is not yet implemented (needs session-scoped state). Fall
		// back to Manual once, with a warning, so users who set `auto` in config
		// still get safe prompting instead of silent allow.
		if (effective == config::PermissionMode::Auto)
		{
			if (!autoWarned)
			{
				spdlog::warn("permission: Auto mode not implemented yet, treating as Manual for now");
				autoWarned = true;
			}
			effective = config::PermissionMode::Manual;
		}

		if (effective == config::PermissionMode::Yolo)
		{
			return true;
		}

		// Manual: ask the callback. A missing callback is treated as Deny so the
		// safe default holds until a UI wires a real approval flow.
		if (!callback)
		{
			spdlog::warn("permission: Manual mode with no approval callback, denying mutating tool '{}'", toolName);
			return false;
		}

		return callback(toolName, args, description) == PermissionDecision::Allow;
	}

} // namespace codeharness::permission

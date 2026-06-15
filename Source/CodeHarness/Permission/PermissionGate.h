#pragma once

#include "Config/ConfigTypes.h"
#include "Permission/PermissionTypes.h"

namespace codeharness::permission
{

	// PermissionGate decides whether a resolved tool may execute. It is the
	// single consumer of `ToolExecution::requiresPermission` in the loop.
	//
	// Mode semantics (MVP):
	//   - Yolo:   every tool is allowed; the approval callback is never invoked.
	//   - Manual: read-only tools (requiresPermission == false) are allowed
	//             without asking; mutating tools invoke the approval callback
	//             and run only if it returns Allow.
	//   - Auto:   not yet implemented; treated as Manual with a one-shot warning
	//             (session-scoped "allow after first approval" needs the Session
	//             module which does not exist yet).
	//
	// The gate holds no per-session state — it is safe to reuse across turns
	// within a single Agent. The approval callback is stored by value; pass a
	// null-constructed ApprovalCallback (or use the Yolo mode) to skip prompts.
	class PermissionGate
	{
	public:
		PermissionGate(config::PermissionMode mode, ApprovalCallback callback);

		// Returns true iff the tool may run. See class doc for per-mode behavior.
		// When `requiresPermission` is false this always returns true.
		bool ShouldRun(bool requiresPermission, std::string_view toolName, const nlohmann::json& args, std::string_view description);

		config::PermissionMode Mode() const { return mode; }

	private:
		config::PermissionMode mode;
		ApprovalCallback callback;
		bool autoWarned = false;
	};

} // namespace codeharness::permission

#pragma once

#include <functional>
#include <string_view>

#include <nlohmann/json.hpp>

#include "Config/ConfigTypes.h"

namespace codeharness::permission
{

	// Outcome of a single permission inquiry. The MVP consumes only Allow/Deny;
	// future modes (e.g. session-scoped Auto) may extend this without breaking
	// the callback signature.
	enum class PermissionDecision
	{
		Allow,
		Deny,
	};

	// Synchronous approval hook invoked by PermissionGate before a tool with
	// `requiresPermission == true` is allowed to run. Implementations (UI/TUI,
	// tests, headless stubs) receive enough context to render a prompt:
	//   - toolName: the tool's Name()
	//   - args:     parsed tool arguments (may be empty object)
	//   - description: human-readable summary from ToolExecution::description
	//
	// Returning Deny causes the loop to skip Execute() and emit a tool result
	// with isError=true. The callback is never invoked for read-only tools or
	// when the gate is in Yolo mode.
	using ApprovalCallback = std::function<PermissionDecision(
		std::string_view toolName, const nlohmann::json& args, std::string_view description)>;

} // namespace codeharness::permission

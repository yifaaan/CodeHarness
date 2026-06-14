#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "Engine/Tool.h"

namespace codeharness::tools
{

	// Owns built-in tool instances and exposes them to the turn loop.
	//
	// For now the manager is intentionally minimal: it registers tools, looks them
	// up by name, and yields the pointer set for `engine::TurnInput::tools`.
	// Active-set selection and MCP/user-tool registration belong to the (future)
	// Agent layer and are deferred.
	class ToolManager
	{
	  public:
		ToolManager() = default;
		~ToolManager() = default;
		ToolManager(const ToolManager &) = delete;
		ToolManager &operator=(const ToolManager &) = delete;

		// Register a tool; the manager takes ownership. Nullptr is ignored.
		void Register(std::unique_ptr<engine::ExecutableTool> tool);

		// Look up a tool by name, or nullptr if not registered.
		engine::ExecutableTool *Find(std::string_view name) const;

		// Pointers to all registered tools, suitable for `engine::TurnInput::tools`.
		std::vector<engine::ExecutableTool *> LoopTools() const;

		// Number of registered tools.
		std::size_t Size() const;

	  private:
		std::vector<std::unique_ptr<engine::ExecutableTool>> tools;
	};

} // namespace codeharness::tools

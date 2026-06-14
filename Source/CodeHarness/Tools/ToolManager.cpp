#include "Tools/ToolManager.h"

#include <utility>

namespace codeharness::tools
{

	void ToolManager::Register(std::unique_ptr<engine::ExecutableTool> tool)
	{
		if (tool)
			tools.push_back(std::move(tool));
	}

	engine::ExecutableTool* ToolManager::Find(std::string_view name) const
	{
		for (const auto& t : tools)
		{
			if (t->Name() == name)
				return t.get();
		}
		return nullptr;
	}

	std::vector<engine::ExecutableTool*> ToolManager::LoopTools() const
	{
		std::vector<engine::ExecutableTool*> result;
		result.reserve(tools.size());
		for (const auto& t : tools)
			result.push_back(t.get());
		return result;
	}

	std::size_t ToolManager::Size() const
	{
		return tools.size();
	}

} // namespace codeharness::tools

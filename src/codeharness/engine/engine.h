#pragma once

#include "codeharness/core/message.h"
#include "codeharness/core/result.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/tool_registry.h"

#include <string>
#include <vector>

namespace codeharness
{

struct EngineOptions
{
    int max_turns = 10;
};

struct RunRequest
{
    std::string prompt;
    EngineOptions options;
};

struct RunResult
{
    std::vector<Message> messages;
    std::string output_text;
};

class Engine
{
public:
    explicit Engine(Provider& provider);
    Engine(Provider& provider, const ToolRegistry& tools);

    auto run(const RunRequest& request) -> Result<RunResult>;

private:
    auto execute_tool_use(const ToolUseBlock& tool_use) const -> ToolResultBlock;

    Provider& provider_;
    const ToolRegistry* tools_ = nullptr;
};

} // namespace codeharness
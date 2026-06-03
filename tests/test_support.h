#pragma once

#include "codeharness/cli/cli.h"
#include "codeharness/commands/command_registry.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/message.h"
#include "codeharness/engine/engine.h"
#include "codeharness/hooks/hook_executor.h"
#include "codeharness/hooks/hook_registry.h"
#include "codeharness/mcp/client_session.h"
#include "codeharness/mcp/json_rpc.h"
#include "codeharness/mcp/stdio_transport.h"
#include "codeharness/mcp/tool_adapter.h"
#include "codeharness/mcp/transport.h"
#include "codeharness/mcp/types.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/plugins/plugin_loader.h"
#include "codeharness/prompts/project_context.h"
#include "codeharness/prompts/system_prompt.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/provider/provider.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/skills/skill_registry.h"
#include "codeharness/tools/bash_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/skill_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/tools/write_file_tool.h"
#include "codeharness/version.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#ifndef CODEHARNESS_FAKE_MCP_SERVER
#define CODEHARNESS_FAKE_MCP_SERVER ""
#endif

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(std::string name);
    ~TempDir();

    TempDir(const TempDir&) = delete;
    auto operator=(const TempDir&) -> TempDir& = delete;
};

auto set_request_input(codeharness::ToolRequest& request, std::string input_json) -> void;
auto read_file_text(const std::filesystem::path& path) -> std::string;
auto init_git_repository_with_head(const std::filesystem::path& repo, std::string_view branch) -> void;

class WriteFileRequestProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override;
};
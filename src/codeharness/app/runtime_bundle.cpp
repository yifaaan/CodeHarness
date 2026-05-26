#include "codeharness/app/runtime_bundle.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <memory>
#include <utility>

#include "codeharness/logging.h"
#include "codeharness/tools/brief_tool.h"
#include "codeharness/tools/config_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/sleep_tool.h"
#include "codeharness/tools/tool_search_tool.h"
#include "codeharness/tools/todo_write_tool.h"
#include "codeharness/tools/write_file_tool.h"

namespace codeharness::app {

    auto RuntimeBundle::default_system_prompt() -> absl::string_view {
        return "You are CodeHarness, a concise coding assistant.";
    }

    auto RuntimeBundle::create(config::Settings settings,
                               std::filesystem::path cwd,
                               std::string system_prompt) -> absl::StatusOr<RuntimeBundle> {
        CH_LOG_DEBUG("RuntimeBundle::create",
                     "cwd={} model={} base_url={} max_tokens={} api_key_present={}", cwd.string(),
                     settings.api.model, settings.api.base_url, settings.api.max_tokens,
                     !settings.api.api_key.empty());

        auto tools = tools::ToolRegistry{};
        tools.register_tool(std::make_unique<tools::ReadFileTool>());
        tools.register_tool(std::make_unique<tools::WriteFileTool>());
        tools.register_tool(std::make_unique<tools::EditFileTool>());
        tools.register_tool(std::make_unique<tools::GlobTool>());
        tools.register_tool(std::make_unique<tools::GrepTool>());
        tools.register_tool(std::make_unique<tools::ToolSearchTool>());
        tools.register_tool(std::make_unique<tools::BriefTool>());
        tools.register_tool(std::make_unique<tools::SleepTool>());
        tools.register_tool(std::make_unique<tools::TodoWriteTool>());
        tools.register_tool(std::make_unique<tools::ConfigTool>());

        auto permissions = permissions::PermissionChecker{settings.permissions};
        auto api = api::OpenAIClient{
            api::OpenAIClientOptions{
                .api_key = settings.api.api_key,
                .base_url = settings.api.base_url,
                .timeout = settings.api.timeout,
            },
        };

        const auto cwd_for_sessions = cwd;
        auto session_storage = services::SessionStorage::for_cwd(cwd_for_sessions);

        CH_LOG_DEBUG("RuntimeBundle::create", "registered_tools={}", tools.list_tools().size());

        return RuntimeBundle{
            std::move(settings), std::move(cwd),         std::move(system_prompt),   std::move(api),
            std::move(tools),    std::move(permissions), std::move(session_storage),
        };
    }

    RuntimeBundle::RuntimeBundle(config::Settings settings,
                                 std::filesystem::path cwd,
                                 std::string system_prompt,
                                 api::OpenAIClient api,
                                 tools::ToolRegistry tools,
                                 permissions::PermissionChecker permissions,
                                 services::SessionStorage session_storage)
        : settings_{std::move(settings)},
          cwd_{std::move(cwd)},
          session_storage_{std::move(session_storage)},
          system_prompt_{std::move(system_prompt)},
          api_{std::move(api)},
          tools_{std::move(tools)},
          permissions_{std::move(permissions)},
          engine_{api_, tools_, permissions_, cwd_, settings_.api.model, system_prompt_} {
        CH_LOG_DEBUG("RuntimeBundle::RuntimeBundle", "initialized cwd={} model={}", cwd_.string(),
                     settings_.api.model);
    }

    RuntimeBundle::RuntimeBundle(RuntimeBundle&& other) noexcept
        : settings_{std::move(other.settings_)},
          cwd_{std::move(other.cwd_)},
          session_storage_{std::move(other.session_storage_)},
          system_prompt_{std::move(other.system_prompt_)},
          api_{std::move(other.api_)},
          tools_{std::move(other.tools_)},
          permissions_{std::move(other.permissions_)},
          engine_{api_, tools_, permissions_, cwd_, settings_.api.model, system_prompt_} {
        CH_LOG_DEBUG("RuntimeBundle::RuntimeBundle", "moved cwd={} model={}", cwd_.string(),
                     settings_.api.model);
    }

    auto RuntimeBundle::set_model(std::string model) -> void {
        CH_LOG_DEBUG("RuntimeBundle::set_model", "from={} to={}", settings_.api.model, model);
        settings_.api.model = std::move(model);
        engine_.set_model(settings_.api.model);
    }

    auto RuntimeBundle::set_system_prompt(std::string system_prompt) -> void {
        CH_LOG_DEBUG("RuntimeBundle::set_system_prompt", "chars={}", system_prompt.size());
        system_prompt_ = std::move(system_prompt);
        engine_.set_system_prompt(system_prompt_);
    }

    auto RuntimeBundle::clear_conversation() noexcept -> void {
        CH_LOG_DEBUG("RuntimeBundle::clear_conversation", "cwd={}", cwd_.string());
        engine_.clear();
    }

}  // namespace codeharness::app

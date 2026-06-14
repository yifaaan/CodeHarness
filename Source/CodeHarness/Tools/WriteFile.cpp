#include "Tools/WriteFile.h"

#include <string>

#include "Host/Host.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"

namespace codeharness::tools
{

	namespace
	{

		std::string GetPath(const nlohmann::json& args)
		{
			return args.value("path", std::string{});
		}

		std::string GetMode(const nlohmann::json& args)
		{
			return args.value("mode", std::string{"overwrite"});
		}

	} // namespace

	std::string WriteFileTool::Description() const
	{
		return "Create or overwrite a file. Set `mode` to \"append\" to add content to the end "
			   "of an existing file (the file is created if it does not exist). Returns the "
			   "number of UTF-8 bytes written.";
	}

	nlohmann::json WriteFileTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "Absolute or relative file path"}}},
			  {"content", {{"type", "string"}, {"description", "The file contents to write."}}},
			  {"mode",
			   {{"type", "string"},
				{"enum", nlohmann::json::array({"overwrite", "append"})},
				{"default", "overwrite"},
				{"description", "Whether to overwrite the file or append to it."}}}}},
			{"required", nlohmann::json::array({"path", "content"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> WriteFileTool::ResolveExecution(const nlohmann::json& args)
	{
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'path' is required");
		auto mode = GetMode(args);
		if (mode != "overwrite" && mode != "append")
		{
			return absl::InvalidArgumentError("'mode' must be \"overwrite\" or \"append\"");
		}
		return engine::ToolExecution{.description = fmt::format("{} {}", mode, path), .requiresPermission = true};
	}

	absl::StatusOr<engine::ToolResult> WriteFileTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'path' is required");
		auto mode = GetMode(args);
		if (mode != "overwrite" && mode != "append")
		{
			return absl::InvalidArgumentError("'mode' must be \"overwrite\" or \"append\"");
		}

		std::string content = args.value("content", std::string{});
		std::string toWrite = std::move(content);

		if (mode == "append")
		{
			auto existing = ctx.host->ReadText(path);
			if (existing.ok())
			{
				toWrite = std::move(*existing) + toWrite;
			}
			// If the file does not exist yet, we simply create it with `toWrite`.
		}

		auto status = ctx.host->WriteText(path, toWrite);
		if (!status.ok())
			return status;

		return engine::ToolResult{.content = fmt::format("wrote {} byte(s) to {}", toWrite.size(), path)};
	}

} // namespace codeharness::tools

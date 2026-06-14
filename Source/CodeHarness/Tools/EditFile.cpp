#include "Tools/EditFile.h"

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
			return args.value("file_path", std::string{});
		}

	} // namespace

	std::string EditFileTool::Description() const
	{
		return "Replace an exact substring in a file. By default `old_string` must match exactly "
			   "once (zero or multiple matches produce an error); set `replace_all` to true to "
			   "replace every occurrence. `old_string` must differ from `new_string`.";
	}

	nlohmann::json EditFileTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"file_path", {{"type", "string"}, {"description", "Absolute or relative file path"}}},
			  {"old_string", {{"type", "string"}, {"description", "The exact text to replace."}}},
			  {"new_string", {{"type", "string"}, {"description", "The replacement text."}}},
			  {"replace_all", {{"type", "boolean"}, {"default", false}, {"description", "Replace every occurrence."}}}}},
			{"required", nlohmann::json::array({"file_path", "old_string", "new_string"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> EditFileTool::ResolveExecution(const nlohmann::json& args)
	{
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'file_path' is required");

		std::string oldString = args.value("old_string", std::string{});
		std::string newString = args.value("new_string", std::string{});
		if (oldString.empty())
			return absl::InvalidArgumentError("'old_string' is required");
		if (oldString == newString)
		{
			return absl::InvalidArgumentError("'old_string' must differ from 'new_string'");
		}
		return engine::ToolExecution{.description = fmt::format("Edit {}", path), .requiresPermission = true};
	}

	absl::StatusOr<engine::ToolResult> EditFileTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'file_path' is required");

		std::string oldString = args.value("old_string", std::string{});
		std::string newString = args.value("new_string", std::string{});
		bool replaceAll = args.value("replace_all", false);
		if (oldString.empty())
			return absl::InvalidArgumentError("'old_string' is required");
		if (oldString == newString)
		{
			return absl::InvalidArgumentError("'old_string' must differ from 'new_string'");
		}

		auto text = ctx.host->ReadText(path);
		if (!text.ok())
			return std::move(text).status();

		// Count occurrences.
		std::size_t count = 0;
		for (std::size_t pos = 0; pos <= text->size();)
		{
			auto found = text->find(oldString, pos);
			if (found == std::string::npos)
				break;
			++count;
			pos = found + oldString.size();
		}

		if (count == 0)
		{
			return absl::NotFoundError("old_string was not found in the file");
		}
		if (!replaceAll && count > 1)
		{
			return absl::AlreadyExistsError("old_string matches multiple locations; set replace_all=true to replace all");
		}

		std::string result;
		result.reserve(text->size() +
					   (newString.size() > oldString.size() ? (newString.size() - oldString.size()) * count : 0));
		std::size_t cursor = 0;
		std::size_t replaced = 0;
		while (true)
		{
			auto found = text->find(oldString, cursor);
			if (found == std::string::npos)
				break;
			result.append(text->data() + cursor, found - cursor);
			result.append(newString);
			cursor = found + oldString.size();
			++replaced;
			if (!replaceAll)
			{
				result.append(text->data() + cursor, text->size() - cursor);
				break;
			}
		}
		if (replaceAll)
		{
			result.append(text->data() + cursor, text->size() - cursor);
		}

		auto status = ctx.host->WriteText(path, result);
		if (!status.ok())
			return status;

		return engine::ToolResult{.content = fmt::format("replaced {} occurrence(s) in {}", replaced, path)};
	}

} // namespace codeharness::tools

#include "Tools/ReadFile.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Host/Host.h"
#include "Tools/ToolOutput.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"

namespace codeharness::tools
{

	namespace
	{

		constexpr int kMaxReadLines = 1000;

		std::string GetPath(const nlohmann::json& args)
		{
			return args.value("path", std::string{});
		}

	} // namespace

	std::string ReadFileTool::Description() const
	{
		return "Read a file from the local filesystem. Returns the contents with 1-based "
			   "line numbers. Use `line_offset` (1-based; negative counts from the end) and "
			   "`n_lines` to page through large files. Binary files are rejected.";
	}

	nlohmann::json ReadFileTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"path", {{"type", "string"}, {"description", "Absolute or relative file path"}}},
			  {"line_offset",
			   {{"type", "integer"},
				{"description", "1-based line number to start reading from. Negative counts from the end."}}},
			  {"n_lines", {{"type", "integer"}, {"description", "Maximum number of lines to return."}}}}},
			{"required", nlohmann::json::array({"path"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> ReadFileTool::ResolveExecution(const nlohmann::json& args)
	{
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'path' is required");
		return engine::ToolExecution{.description = fmt::format("Read {}", path), .requiresPermission = false};
	}

	absl::StatusOr<engine::ToolResult> ReadFileTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto path = GetPath(args);
		if (path.empty())
			return absl::InvalidArgumentError("'path' is required");

		int lineOffset = args.value("line_offset", 0);
		int nLines = args.value("n_lines", 0);
		if (nLines > kMaxReadLines)
			nLines = kMaxReadLines;

		auto text = ctx.host->ReadText(path);
		if (!text.ok())
			return std::move(text).status();

		if (text->find('\0') != std::string::npos)
		{
			return absl::InvalidArgumentError("file appears to be binary; refusing to read");
		}

		auto lines = SplitLines(*text);
		int total = static_cast<int>(lines.size());

		int startIdx = 0;
		if (lineOffset > 0)
		{
			startIdx = lineOffset - 1;
		}
		else if (lineOffset < 0)
		{
			startIdx = total + lineOffset;
		}
		if (startIdx < 0)
			startIdx = 0;
		if (startIdx > total)
			startIdx = total;

		int endIdx = total;
		if (nLines > 0)
			endIdx = std::min(startIdx + nLines, total);

		std::vector<std::string> sliced(lines.begin() + startIdx, lines.begin() + endIdx);
		std::string out = NumberLines(sliced, startIdx + 1);
		out = TruncateOutput(out);
		return engine::ToolResult{.content = std::move(out)};
	}

} // namespace codeharness::tools

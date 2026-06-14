#include "Tools/Glob.h"

#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"
#include "Host/Host.h"
#include "Host/HostTypes.h"
#include "Tools/ToolOutput.h"

namespace codeharness::tools
{

	namespace
	{

		constexpr int kMaxResults = 1000;

		bool IsTooBroad(std::string_view pattern)
		{
			return pattern == "**" || pattern == "**/*" || pattern == "*" || pattern.empty();
		}

	} // namespace

	std::string GlobTool::Description() const
	{
		return "Find files matching a glob pattern (e.g. \"src/**/*.cpp\"). Returns matching "
			   "paths, one per line. Overly broad patterns are rejected.";
	}

	nlohmann::json GlobTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"pattern", {{"type", "string"}, {"description", "Glob pattern, e.g. \"src/**/*.cpp\"."}}},
			  {"path", {{"type", "string"}, {"description", "Directory to search in. Defaults to the current directory."}}},
			  {"include_dirs",
			   {{"type", "boolean"}, {"default", true}, {"description", "Include directories in results."}}}}},
			{"required", nlohmann::json::array({"pattern"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> GlobTool::ResolveExecution(const nlohmann::json &args)
	{
		auto pattern = args.value("pattern", std::string{});
		if (IsTooBroad(pattern))
		{
			return absl::InvalidArgumentError("'pattern' is too broad; provide a more specific glob");
		}
		return engine::ToolExecution{.description = fmt::format("Glob {}", pattern), .requiresPermission = false};
	}

	absl::StatusOr<engine::ToolResult> GlobTool::Execute(const nlohmann::json &args, const engine::ToolContext &ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto pattern = args.value("pattern", std::string{});
		if (IsTooBroad(pattern))
		{
			return absl::InvalidArgumentError("'pattern' is too broad; provide a more specific glob");
		}

		std::string path = args.value("path", std::string{});
		bool includeDirs = args.value("include_dirs", true);

		host::GlobOptions opts;
		opts.includeDirs = includeDirs;

		auto matches = ctx.host->Glob(pattern, path, opts);
		if (!matches.ok())
			return std::move(matches).status();

		if (static_cast<int>(matches->size()) > kMaxResults)
		{
			matches->resize(kMaxResults);
		}

		std::string out;
		for (const auto &m : *matches)
		{
			out += m;
			out += '\n';
		}
		if (out.empty())
			out = "(no matches)\n";
		out = TruncateOutput(out);
		return engine::ToolResult{.content = std::move(out)};
	}

} // namespace codeharness::tools

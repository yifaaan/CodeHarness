#include "Tools/Grep.h"

#include <re2/re2.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Host/Host.h"
#include "Host/HostTypes.h"
#include "Tools/ToolOutput.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"

namespace codeharness::tools
{

	namespace
	{

		constexpr std::size_t kMaxFileSize = 10 * 1024 * 1024; // skip files larger than 10 MB

		bool IsOutputMode(std::string_view m)
		{
			return m == "content" || m == "files_with_matches" || m == "count";
		}

		// Filename match for simple glob filters ("*.cpp", "*.h", "*"). We match on the
		// filename only, since the caller wants file-type filters regardless of depth.
		bool MatchesFilename(const std::string& filename, const std::string& fileGlob)
		{
			if (fileGlob.empty() || fileGlob == "*")
				return true;
			if (fileGlob.rfind("*.", 0) == 0)
			{
				std::string suffix = fileGlob.substr(1); // e.g. ".cpp"
				return filename.size() >= suffix.size() &&
					   filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
			}
			return filename == fileGlob;
		}

	} // namespace

	std::string GrepTool::Description() const
	{
		return "Search file contents with a regular expression (RE2 syntax). By default returns "
			   "matching lines with file and line number. Set `output_mode` to \"files_with_matches\" "
			   "for just the file list, or \"count\" for per-file match counts. Use `glob` to filter "
			   "files (e.g. \"*.cpp\").";
	}

	nlohmann::json GrepTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"pattern", {{"type", "string"}, {"description", "RE2 regular expression to search for."}}},
			  {"path", {{"type", "string"}, {"description", "Directory to search in. Defaults to the current directory."}}},
			  {"glob", {{"type", "string"}, {"description", "File-name filter, e.g. \"*.cpp\" or \"*.h\"."}}},
			  {"ignore_case", {{"type", "boolean"}, {"default", false}, {"description", "Case-insensitive match."}}},
			  {"line_numbers", {{"type", "boolean"}, {"default", true}, {"description", "Show line numbers for matches."}}},
			  {"before_context", {{"type", "integer"}, {"description", "Lines of context before a match."}}},
			  {"after_context", {{"type", "integer"}, {"description", "Lines of context after a match."}}},
			  {"context", {{"type", "integer"}, {"description", "Lines of context before and after a match."}}},
			  {"output_mode",
			   {{"type", "string"},
				{"enum", nlohmann::json::array({"content", "files_with_matches", "count"})},
				{"default", "content"}}},
			  {"head_limit",
			   {{"type", "integer"},
				{"default", 250},
				{"description", "Maximum number of result lines/files. 0 means unlimited."}}}}},
			{"required", nlohmann::json::array({"pattern"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> GrepTool::ResolveExecution(const nlohmann::json& args)
	{
		auto pattern = args.value("pattern", std::string{});
		if (pattern.empty())
			return absl::InvalidArgumentError("'pattern' is required");
		std::string mode = args.value("output_mode", std::string{"content"});
		if (!IsOutputMode(mode))
		{
			return absl::InvalidArgumentError("'output_mode' must be content, files_with_matches, or count");
		}
		return engine::ToolExecution{.description = fmt::format("Grep /{}/", pattern), .requiresPermission = false};
	}

	absl::StatusOr<engine::ToolResult> GrepTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto pattern = args.value("pattern", std::string{});
		if (pattern.empty())
			return absl::InvalidArgumentError("'pattern' is required");
		std::string mode = args.value("output_mode", std::string{"content"});
		if (!IsOutputMode(mode))
		{
			return absl::InvalidArgumentError("'output_mode' must be content, files_with_matches, or count");
		}

		bool ignoreCase = args.value("ignore_case", false);
		RE2::Options opts;
		opts.set_case_sensitive(!ignoreCase);
		RE2 re(pattern, opts);
		if (!re.ok())
			return absl::InvalidArgumentError(fmt::format("invalid regex: {}", re.error()));

		std::string path = args.value("path", std::string{});
		std::string fileGlob = args.value("glob", std::string{});
		bool lineNumbers = args.value("line_numbers", true);

		int before = args.value("before_context", 0);
		int after = args.value("after_context", 0);
		int context = args.value("context", 0);
		if (context > 0)
		{
			before = std::max(before, context);
			after = std::max(after, context);
		}

		int headLimit = args.value("head_limit", 250);

		// Enumerate candidate files: combine a non-recursive pass (root-level files)
		// with a recursive one (nested files), because a single "**/*" pattern does
		// not reliably yield root-level entries with the glob backend.
		host::GlobOptions gopts;
		gopts.includeDirs = false;

		std::vector<std::string> candidates;
		for (const char* pat : {"*", "**/*"})
		{
			auto found = ctx.host->Glob(pat, path, gopts);
			if (found.ok())
			{
				for (auto& f : *found)
					candidates.push_back(std::move(f));
			}
		}
		std::sort(candidates.begin(), candidates.end());
		candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

		auto& files = candidates;

		std::string out;
		int remaining = headLimit;
		bool limitHit = false;
		bool prevActive = false;

		for (const auto& file : files)
		{
			if (limitHit)
				break;
			if (ctx.stopToken.stop_requested())
			{
				out += "... [search interrupted]\n";
				break;
			}

			if (!MatchesFilename(std::filesystem::path(file).filename().string(), fileGlob))
			{
				continue;
			}

			auto text = ctx.host->ReadText(file);
			if (!text.ok())
				continue; // skip unreadable files
			if (text->find('\0') != std::string::npos)
				continue; // skip binary files
			if (text->size() > kMaxFileSize)
				continue;

			auto lines = SplitLines(*text);
			int n = static_cast<int>(lines.size());
			std::vector<char> kind(n, 0); // 0 = none, 1 = context, 2 = match
			int matchCount = 0;

			for (int i = 0; i < n; ++i)
			{
				if (RE2::PartialMatch(lines[i], re))
				{
					kind[i] = 2;
					++matchCount;
					int lo = std::max(0, i - before);
					int hi = std::min(n - 1, i + after);
					for (int j = lo; j <= hi; ++j)
					{
						if (kind[j] == 0)
							kind[j] = 1;
					}
				}
			}

			if (mode == "files_with_matches")
			{
				if (matchCount > 0)
				{
					out += file;
					out += '\n';
					if (remaining > 0 && --remaining == 0)
						limitHit = true;
				}
				continue;
			}
			if (mode == "count")
			{
				if (matchCount > 0)
				{
					out += fmt::format("{}:{}\n", file, matchCount);
					if (remaining > 0 && --remaining == 0)
						limitHit = true;
				}
				continue;
			}

			// content mode
			for (int i = 0; i < n; ++i)
			{
				if (kind[i] == 0)
				{
					prevActive = false;
					continue;
				}
				if (!prevActive && !out.empty())
				{
					out += "--\n";
				}
				char sep = (kind[i] == 2) ? ':' : '-';
				if (lineNumbers)
				{
					out += fmt::format("{}{}{}{}{}\n", file, sep, i + 1, sep, lines[i]);
				}
				else
				{
					out += fmt::format("{}{}{}\n", file, sep, lines[i]);
				}
				prevActive = true;
				if (remaining > 0 && --remaining == 0)
				{
					limitHit = true;
					break;
				}
			}
		}

		if (limitHit)
			out += fmt::format("... [head_limit of {} reached]\n", headLimit);
		if (out.empty())
			out = "(no matches)\n";
		out = TruncateOutput(out);
		return engine::ToolResult{.content = std::move(out)};
	}

} // namespace codeharness::tools

#include "Session/WorkdirKey.h"

#include <cstdint>

#include "fmt/format.h"

namespace codeharness::session
{

	namespace
	{

		bool IsIllegalPathChar(char c)
		{
			// Cover both POSIX and Windows illegal chars: path separators and the
			// set reserved by common filesystems. Also strip whitespace/colons so
			// the resulting directory name is portable. We replace these with '_'.
			return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
				   c == '"' || c == '<' || c == '>' || c == '|' || c == '\0' ||
				   c == ' ' || c == '\t';
		}

		std::string Sanitize(std::string_view path)
		{
			std::string out;
			out.reserve(path.size());
			for (char c : path)
			{
				if (IsIllegalPathChar(c))
				{
					// Collapse runs of separators into a single underscore and
					// drop leading separators for a clean key.
					if (!out.empty() && out.back() != '_')
					{
						out.push_back('_');
					}
				}
				else
				{
					out.push_back(c);
				}
			}
			while (!out.empty() && out.back() == '_')
			{
				out.pop_back();
			}
			while (!out.empty() && out.front() == '_')
			{
				out.erase(out.begin());
			}
			return out;
		}

		std::string Fnv1a64Hex(std::string_view data)
		{
			// FNV-1a 64-bit. Public domain algorithm; no dependency.
			std::uint64_t hash = 0xcbf29ce484222325ULL;
			for (unsigned char c : data)
			{
				hash ^= c;
				hash *= 0x100000001b3ULL;
			}
			return fmt::format("{:016x}", hash);
		}

	} // namespace

	std::string EncodeWorkdirKey(std::string_view absoluteWorkdir)
	{
		auto sanitized = Sanitize(absoluteWorkdir);
		if (sanitized.empty())
		{
			// Degenerate input — fall back to just the hash.
			return Fnv1a64Hex(absoluteWorkdir);
		}
		return fmt::format("{}-{}", sanitized, Fnv1a64Hex(absoluteWorkdir));
	}

} // namespace codeharness::session

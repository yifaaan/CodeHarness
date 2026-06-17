#include "Skills/SkillScanner.h"

#include "Skills/SkillParser.h"
#include "absl/status/status.h"
#include "spdlog/spdlog.h"

#include <algorithm>

namespace codeharness::skills
{

	namespace
	{
		bool HasExtension(const std::string& name, const std::string& ext)
		{
			if (name.size() < ext.size())
				return false;
			return name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
		}

		std::string JoinPath(const std::string& base, const std::string& name)
		{
			if (base.empty())
				return name;
			if (base.back() == '/' || base.back() == '\\')
				return base + name;
			return base + "/" + name;
		}

		bool ShouldIgnore(const std::string& name)
		{
			if (name.empty())
				return true;
			if (name[0] == '.')
				return true;
			if (name == "node_modules")
				return true;
			return false;
		}

		bool IsDirectory(const host::StatResult& stat)
		{
			return (stat.stMode & 0170000) == 0040000;
		}

		bool IsRegularFile(const host::StatResult& stat)
		{
			return (stat.stMode & 0170000) == 0100000;
		}

		void ScanDirectory(const std::string& dirPath, int depth, host::Host* host, std::vector<SkillDefinition>& results, SkillSource source)
		{
			if (depth > SkillScanner::MAX_DEPTH)
				return;

			auto iterResult = host->Iterdir(dirPath);
			if (!iterResult.ok())
				return;

			std::vector<std::string> subdirs;

			for (const auto& entry : *iterResult)
			{
				if (ShouldIgnore(entry))
					continue;

				auto fullPath = JoinPath(dirPath, entry);
				auto statResult = host->Stat(fullPath);
				if (!statResult.ok())
					continue;

				if (IsDirectory(*statResult))
				{
					auto skillMdPath = JoinPath(fullPath, "SKILL.md");
					auto skillMdStat = host->Stat(skillMdPath);
					if (skillMdStat.ok() && IsRegularFile(*skillMdStat))
					{
						auto contentResult = host->ReadText(skillMdPath);
						if (contentResult.ok())
						{
							auto parseResult = SkillParser::Parse(*contentResult, skillMdPath, source);
							if (parseResult.ok())
								results.push_back(*parseResult);
							else
								spdlog::warn("Failed to parse skill {}: {}", skillMdPath, parseResult.status().ToString());
						}
					}
					else
					{
						subdirs.push_back(entry);
					}
				}
				else if (IsRegularFile(*statResult) && HasExtension(entry, ".md"))
				{
					if (entry == "SKILL.md")
						continue;

					auto contentResult = host->ReadText(fullPath);
					if (contentResult.ok())
					{
						auto parseResult = SkillParser::Parse(*contentResult, fullPath, source);
						if (parseResult.ok())
							results.push_back(*parseResult);
						else
							spdlog::warn("Failed to parse skill {}: {}", fullPath, parseResult.status().ToString());
					}
				}
			}

			for (const auto& subdir : subdirs)
			{
				ScanDirectory(JoinPath(dirPath, subdir), depth + 1, host, results, source);
			}
		}
	} // namespace

	std::vector<SkillDefinition> SkillScanner::Scan(std::span<const SkillRoot> roots, host::Host* host)
	{
		std::vector<SkillDefinition> allSkills;

		for (const auto& root : roots)
		{
			auto rootSkills = ScanRoot(root, host);
			allSkills.insert(allSkills.end(), rootSkills.begin(), rootSkills.end());
		}

		return allSkills;
	}

	std::vector<SkillDefinition> SkillScanner::ScanRoot(const SkillRoot& root, host::Host* host)
	{
		std::vector<SkillDefinition> results;

		auto statResult = host->Stat(root.path);
		if (!statResult.ok() || !IsDirectory(*statResult))
			return results;

		ScanDirectory(root.path, 0, host, results, root.source);

		return results;
	}

} // namespace codeharness::skills

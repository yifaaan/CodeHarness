#pragma once

#include <string>
#include <string_view>

#include "Skills/SkillTypes.h"
#include "absl/status/statusor.h"

namespace codeharness::skills
{

	class SkillParser
	{
	public:
		static absl::StatusOr<SkillDefinition> Parse(std::string_view content, const std::string& filePath, SkillSource source);

	private:
		static absl::StatusOr<SkillMetadata> ParseFrontmatter(std::string_view yaml);
		static std::string_view ParseBody(std::string_view content);
		static bool FindFrontmatterBounds(std::string_view content, std::string_view& yaml, std::string_view& body);

		static std::string ExtractDir(const std::string& filePath);
		static std::string ExtractName(const std::string& filePath);
	};

} // namespace codeharness::skills

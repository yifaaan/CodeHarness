#pragma once

#include <span>
#include <vector>

#include "Host/Host.h"
#include "Skills/SkillTypes.h"

namespace codeharness::skills
{

	class SkillScanner
	{
	public:
		static std::vector<SkillDefinition> Scan(std::span<const SkillRoot> roots, host::Host* host);

		static constexpr int MAX_DEPTH = 8;

	private:
		static std::vector<SkillDefinition> ScanRoot(const SkillRoot& root, host::Host* host);
	};

} // namespace codeharness::skills

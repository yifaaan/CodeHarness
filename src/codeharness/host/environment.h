#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::host
{

struct EnvironmentResult
{
	std::string shellPath;
	std::string shellName;
	bool isWindows = false;
	std::vector<std::string> pathDirs;
};

EnvironmentResult DetectEnvironment();
std::string ProbeShell();

} // namespace codeharness::host
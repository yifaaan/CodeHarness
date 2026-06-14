#include "environment.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace codeharness::host
{

#ifdef _WIN32
namespace
{

std::string FindGitOnPath()
{
	const char* pathEnv = std::getenv("PATH");
	if (!pathEnv)
		return {};

	std::string pathStr(pathEnv);
	std::vector<std::string> dirs;
	size_t start = 0, end;
	while ((end = pathStr.find(';', start)) != std::string::npos)
	{
		dirs.push_back(pathStr.substr(start, end - start));
		start = end + 1;
	}
	dirs.push_back(pathStr.substr(start));

	for (const auto& dir : dirs)
	{
		std::error_code ec;
		auto gitExe = std::filesystem::path(dir) / "git.exe";
		if (std::filesystem::exists(gitExe, ec))
		{
			return gitExe.string();
		}
	}
	return {};
}

std::string FindGitInProgramFiles()
{
	std::vector<std::string> candidates = {
		"C:\\Program Files\\Git\\bin\\git.exe",
		"C:\\Program Files (x86)\\Git\\bin\\git.exe",
	};

	const char* localAppData = std::getenv("LOCALAPPDATA");
	if (localAppData)
	{
		candidates.push_back(std::string(localAppData) + "\\Programs\\Git\\bin\\git.exe");
	}

	for (const auto& candidate : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec))
		{
			return candidate;
		}
	}
	return {};
}

std::string ResolveGitBashPath(const std::string& gitExe)
{
	std::string cmd = "\"" + gitExe + "\" --exec-path";
	FILE* pipe = _popen(cmd.c_str(), "r");
	if (!pipe)
		return {};

	char buffer[4096];
	std::string result;
	while (fgets(buffer, sizeof(buffer), pipe))
	{
		result += buffer;
	}
	_pclose(pipe);

	result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
	result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

	if (result.empty())
		return {};

	std::filesystem::path execPath(result);
	auto gitRoot = execPath.parent_path().parent_path().parent_path();
	auto bashCandidate = gitRoot / "bin" / "bash.exe";
	std::error_code ec;
	if (std::filesystem::exists(bashCandidate, ec))
	{
		return bashCandidate.string();
	}

	auto mingwBash = execPath.parent_path() / "bash.exe";
	if (std::filesystem::exists(mingwBash, ec))
	{
		return mingwBash.string();
	}

	return {};
}

} // namespace
#endif

std::string ProbeShell()
{
	const char* kimShell = std::getenv("KIMI_SHELL_PATH");
	if (kimShell && kimShell[0])
	{
		std::error_code ec;
		if (std::filesystem::exists(kimShell, ec))
		{
			return kimShell;
		}
	}

#ifdef _WIN32
	auto gitExe = FindGitOnPath();
	if (gitExe.empty())
	{
		gitExe = FindGitInProgramFiles();
	}

	if (!gitExe.empty())
	{
		auto bashPath = ResolveGitBashPath(gitExe);
		if (!bashPath.empty())
		{
			return bashPath;
		}
	}

	std::vector<std::string> bashCandidates = {
		"C:\\Program Files\\Git\\bin\\bash.exe",
		"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
	};
	const char* localAppData = std::getenv("LOCALAPPDATA");
	if (localAppData)
	{
		bashCandidates.push_back(std::string(localAppData) + "\\Programs\\Git\\bin\\bash.exe");
	}
	const char* userprofile = std::getenv("USERPROFILE");
	if (userprofile)
	{
		bashCandidates.push_back(std::string(userprofile) + "\\scoop\\apps\\git\\current\\bin\\bash.exe");
	}

	for (const auto& candidate : bashCandidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec))
		{
			return candidate;
		}
	}

	return "cmd.exe";
#else
	std::vector<std::string> candidates = {"/bin/bash", "/usr/bin/bash", "/usr/local/bin/bash", "/bin/sh"};
	for (const auto& candidate : candidates)
	{
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec))
		{
			return candidate;
		}
	}
	return "/bin/sh";
#endif
}

EnvironmentResult DetectEnvironment()
{
	EnvironmentResult result;
	result.shellPath = ProbeShell();

	auto shellFilename = std::filesystem::path(result.shellPath).filename().string();
	std::transform(shellFilename.begin(), shellFilename.end(), shellFilename.begin(), ::tolower);
	if (shellFilename == "bash.exe" || shellFilename == "bash")
	{
		result.shellName = "bash";
	}
	else if (shellFilename == "cmd.exe" || shellFilename == "cmd")
	{
		result.shellName = "cmd";
	}
	else
	{
		result.shellName = "sh";
	}

#ifdef _WIN32
	result.isWindows = true;
	const char* pathEnv = std::getenv("PATH");
	if (pathEnv)
	{
		std::string pathStr(pathEnv);
		size_t start = 0, end;
		while ((end = pathStr.find(';', start)) != std::string::npos)
		{
			result.pathDirs.push_back(pathStr.substr(start, end - start));
			start = end + 1;
		}
		result.pathDirs.push_back(pathStr.substr(start));
	}
#else
	result.isWindows = false;
	const char* pathEnv = std::getenv("PATH");
	if (pathEnv)
	{
		std::string pathStr(pathEnv);
		size_t start = 0, end;
		while ((end = pathStr.find(':', start)) != std::string::npos)
		{
			result.pathDirs.push_back(pathStr.substr(start, end - start));
			start = end + 1;
		}
		result.pathDirs.push_back(pathStr.substr(start));
	}
#endif

	return result;
}

} // namespace codeharness::host
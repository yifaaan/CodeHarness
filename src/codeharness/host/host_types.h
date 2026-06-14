#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace codeharness::host
{

struct StatResult
{
	uint32_t stMode = 0;
	uint64_t stIno = 0;
	uint64_t stDev = 0;
	uint16_t stNlink = 0;
	uint32_t stUid = 0;
	uint32_t stGid = 0;
	int64_t stSize = 0;
	int64_t stAtime = 0;
	int64_t stMtime = 0;
	int64_t stCtime = 0;
};

struct GlobOptions
{
	std::filesystem::path cwd;
	bool includeDirs = true;
	int maxDepth = -1;
};

struct MkdirOptions
{
	bool existOk = true;
	bool recursive = false;
};

} // namespace codeharness::host
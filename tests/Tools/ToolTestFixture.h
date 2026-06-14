#pragma once

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Host/LocalHost.h"

namespace host = codeharness::host;

// Shared fixture for tool tests: a LocalHost chdir'd into a unique temp
// directory, with small helpers to write/read scratch files.
struct ToolFixture
{
	host::LocalHost host;
	std::filesystem::path tmp_dir;

	ToolFixture()
	{
		tmp_dir = std::filesystem::temp_directory_path() / ("codeharness_tools_" + std::to_string(std::time(nullptr)));
		std::filesystem::create_directories(tmp_dir);
		(void)host.Chdir(tmp_dir.string());
	}

	~ToolFixture()
	{
		std::error_code ec;
		std::filesystem::remove_all(tmp_dir, ec);
	}

	void WriteFile(const std::string &rel, const std::string &content)
	{
		auto full = tmp_dir / rel;
		std::filesystem::create_directories(full.parent_path());
		std::ofstream f(full, std::ios::binary);
		f << content;
	}

	std::string ReadFile(const std::string &rel)
	{
		auto full = tmp_dir / rel;
		std::ifstream f(full, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	}

	bool Exists(const std::string &rel)
	{
		std::error_code ec;
		return std::filesystem::exists(tmp_dir / rel, ec);
	}
};

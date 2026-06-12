#pragma once

#include "codeharness/core/error.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace codeharness
{

struct ContextFile
{
    std::filesystem::path path;
    std::string content;
};

struct ProjectContextOptions
{
    std::vector<std::string> file_names = {"AGENTS.md", "CLAUDE.md"};
    std::size_t max_total_chars = 32000;
};

auto load_project_context_files(const std::filesystem::path& cwd, ProjectContextOptions options = {})
    -> absl::StatusOr<std::vector<ContextFile>>;

} // namespace codeharness

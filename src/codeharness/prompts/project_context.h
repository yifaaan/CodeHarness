#pragma once

#include "codeharness/core/result.h"

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

class ProjectContextLoader
{
public:
    explicit ProjectContextLoader(ProjectContextOptions options = {});

    auto load(const std::filesystem::path& cwd) const -> Result<std::vector<ContextFile>>;

private:
    ProjectContextOptions options_;
};

auto load_project_context_files(const std::filesystem::path& cwd, ProjectContextOptions options = {})
    -> Result<std::vector<ContextFile>>;

} // namespace codeharness

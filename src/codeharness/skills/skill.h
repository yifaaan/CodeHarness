#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace codeharness
{

struct SkillDefinition
{
    std::string name;
    std::string description;
    std::string content;
    std::string source;
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> base_dir;
    std::optional<std::string> command_name;
    std::optional<std::string> display_name;
    std::vector<std::string> aliases;
    bool user_invocable = true;
    bool disable_model_invocation = false;
    std::optional<std::string> model;
    std::optional<std::string> argument_hint;
};

} // namespace codeharness

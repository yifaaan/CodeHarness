#include "codeharness/mailbox/team_lifecycle.h"

#include "codeharness/core/json_parse.h"
#include "codeharness/core/paths.h"
#include "codeharness/core/time.h"
#include "codeharness/tools/text_file.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <string>
#include <system_error>

namespace codeharness::mailbox
{

auto to_json(nlohmann::json& output, const TeamMember& member) -> void
{
    output = nlohmann::json{
        {"agent_id", member.agent_id},
        {"name", member.name},
        {"backend_type", member.backend_type},
        {"joined_at", member.joined_at},
    };
}

auto from_json(const nlohmann::json& input, TeamMember& member) -> void
{
    input.at("agent_id").get_to(member.agent_id);
    input.at("name").get_to(member.name);
    input.at("backend_type").get_to(member.backend_type);
    input.at("joined_at").get_to(member.joined_at);
}

auto to_json(nlohmann::json& output, const TeamFile& file) -> void
{
    output = nlohmann::json{
        {"name", file.name},
        {"description", file.description},
        {"created_at", file.created_at},
        {"lead_agent_id", file.lead_agent_id},
        {"members", file.members},
    };
}

auto from_json(const nlohmann::json& input, TeamFile& file) -> void
{
    input.at("name").get_to(file.name);
    file.description = input.value("description", "");
    input.at("created_at").get_to(file.created_at);
    file.lead_agent_id = input.value("lead_agent_id", "");

    file.members.clear();
    if (!input.contains("members") || !input.at("members").is_object())
    {
        return;
    }

    for (const auto& [key, value] : input.at("members").items())
    {
        try
        {
            file.members.emplace(key, value.get<TeamMember>());
        }
        catch (const std::exception&)
        {
        }
    }
}

auto default_teams_root() -> std::optional<std::filesystem::path>
{
    const auto data_dir = default_codeharness_data_dir();
    if (!data_dir)
    {
        return std::nullopt;
    }
    return *data_dir / "teams";
}


auto sanitize_team_name(std::string_view name) -> std::string
{
    auto transform_char = [](auto ch) {
        return std::isalnum(ch) ? std::tolower(ch) : '-';
    };

    auto view = name | std::views::transform(transform_char);
    return {view.begin(), view.end()};
}

auto is_valid_team_name(std::string_view name) -> bool
{
    if (name.empty() || name == "." || name == "..")
    {
        return false;
    }

    if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos)
    {
        return false;
    }

    return std::ranges::any_of(name, [](auto ch) { return std::isalnum(ch); });
}

namespace
{

constexpr std::string_view TEAM_FILE_NAME = "team.json";

auto team_file_path(const std::filesystem::path& root, std::string_view team_name) -> std::filesystem::path
{
    return root / team_name / TEAM_FILE_NAME;
}

auto read_team_file_from_disk(const std::filesystem::path& path) -> Result<std::optional<TeamFile>>
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::optional<TeamFile>{};
    }

    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        return fail<std::optional<TeamFile>>(ErrorKind::Io, "team: failed to open file: " + path.string());
    }

    try
    {
        nlohmann::json json;
        file >> json;
        return std::optional<TeamFile>{json.get<TeamFile>()};
    }
    catch (const std::exception& e)
    {
        return fail<std::optional<TeamFile>>(ErrorKind::Io, std::string{"team: corrupted team.json: "} + e.what());
    }
}

} // namespace

TeamLifecycleManager::TeamLifecycleManager(std::filesystem::path root) : root_{std::move(root)}
{
}

auto TeamLifecycleManager::root() const -> const std::filesystem::path&
{
    return root_;
}

auto TeamLifecycleManager::team_dir(std::string_view team_name) const -> std::filesystem::path
{
    return root_ / team_name;
}

auto TeamLifecycleManager::create_team(std::string_view name, std::string_view description) -> Result<TeamFile>
{
    if (!is_valid_team_name(name))
    {
        return fail<TeamFile>(ErrorKind::InvalidArgument, "team: invalid team name: " + std::string{name});
    }

    const auto path = team_file_path(root_, name);
    std::error_code error;
    if (std::filesystem::exists(path, error))
    {
        return fail<TeamFile>(ErrorKind::AlreadyExists, "team: team already exists: " + std::string{name});
    }

    TeamFile team;
    team.name = name;
    team.description = description;
    team.created_at = utc_timestamp_seconds();

    if (auto dir_result = ensure_directory(root_ / name, "team directory"); !dir_result)
    {
        return nonstd::make_unexpected(dir_result.error());
    }

    const auto json = nlohmann::json(team).dump(2);
    if (auto write_result = atomic_write_text_file(path, json); !write_result)
    {
        return nonstd::make_unexpected(write_result.error());
    }

    return team;
}

auto TeamLifecycleManager::delete_team(std::string_view name) -> Result<void>
{
    const auto dir = root_ / name;
    const auto path = dir / TEAM_FILE_NAME;

    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return fail<void>(ErrorKind::NotFound, "team: team not found: " + std::string{name});
    }

    std::error_code remove_error;
    std::filesystem::remove_all(dir, remove_error);
    if (remove_error)
    {
        return fail<void>(ErrorKind::Io, "team: failed to delete team directory: " + remove_error.message());
    }

    return {};
}

auto TeamLifecycleManager::get_team(std::string_view name) const -> Result<std::optional<TeamFile>>
{
    return read_team_file_from_disk(team_file_path(root_, name));
}

auto TeamLifecycleManager::list_teams() const -> Result<std::vector<TeamFile>>
{
    std::error_code error;
    if (!std::filesystem::exists(root_, error))
    {
        return std::vector<TeamFile>{};
    }

    std::vector<TeamFile> teams;

    for (const auto& entry : std::filesystem::directory_iterator{root_, error})
    {
        if (!entry.is_directory())
        {
            continue;
        }

        auto result = read_team_file_from_disk(entry.path() / TEAM_FILE_NAME);
        if (result && result->has_value())
        {
            teams.push_back(std::move(**result));
        }
    }

    std::ranges::sort(teams, {}, &TeamFile::name);

    return teams;
}

auto TeamLifecycleManager::add_member(std::string_view team_name, TeamMember member) -> Result<TeamFile>
{
    auto team = require_team(team_name);
    if (!team)
    {
        return nonstd::make_unexpected(team.error());
    }

    if (member.joined_at.empty())
    {
        member.joined_at = utc_timestamp_seconds();
    }

    team->members.insert_or_assign(member.agent_id, std::move(member));

    if (auto save_result = save_team(*team); !save_result)
    {
        return nonstd::make_unexpected(save_result.error());
    }

    return team;
}

auto TeamLifecycleManager::remove_member(std::string_view team_name, std::string_view agent_id) -> Result<TeamFile>
{
    auto team = require_team(team_name);
    if (!team)
    {
        return nonstd::make_unexpected(team.error());
    }

    if (team->members.erase(std::string{agent_id}) == 0)
    {
        return fail<TeamFile>(ErrorKind::NotFound,
            "team: agent '" + std::string{agent_id} + "' is not a member of team '" + std::string{team_name} + "'");
    }

    if (auto save_result = save_team(*team); !save_result)
    {
        return nonstd::make_unexpected(save_result.error());
    }

    return team;
}

auto TeamLifecycleManager::set_lead_agent(std::string_view team_name, std::string_view agent_id) -> Result<TeamFile>
{
    auto team = require_team(team_name);
    if (!team)
    {
        return nonstd::make_unexpected(team.error());
    }

    team->lead_agent_id = agent_id;

    if (auto save_result = save_team(*team); !save_result)
    {
        return nonstd::make_unexpected(save_result.error());
    }

    return team;
}

auto TeamLifecycleManager::require_team(std::string_view team_name) const -> Result<TeamFile>
{
    auto result = read_team_file_from_disk(team_file_path(root_, team_name));
    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    if (!result->has_value())
    {
        return fail<TeamFile>(ErrorKind::NotFound, "team: team not found: " + std::string{team_name});
    }

    return std::move(**result);
}

auto TeamLifecycleManager::save_team(const TeamFile& team) const -> Result<void>
{
    const auto json = nlohmann::json(team).dump(2);
    return atomic_write_text_file(team_file_path(root_, team.name), json);
}

} // namespace codeharness::mailbox

#include "codeharness/services/cron.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "codeharness/config/paths.h"

namespace codeharness::services::cron {
namespace {

    [[nodiscard]] auto registry_path(bool create_if_missing = true) -> std::filesystem::path {
        return config::paths::user_data_root(create_if_missing) / "cron_jobs.json";
    }

}  // namespace

    auto to_json(nlohmann::json& value, const CronJob& job) -> void {
        value = nlohmann::json{
            {"name", job.name},
            {"schedule", job.schedule},
            {"command", job.command},
            {"cwd", job.cwd.string()},
        };
    }

    auto from_json(const nlohmann::json& value, CronJob& job) -> void {
        job.name = value.value("name", std::string{});
        job.schedule = value.value("schedule", std::string{});
        job.command = value.value("command", std::string{});
        job.cwd = std::filesystem::path{value.value("cwd", std::string{})};
    }

    auto load_cron_jobs() -> std::vector<CronJob> {
        const auto path = registry_path(false);
        if (!std::filesystem::exists(path)) {
            return {};
        }

        std::ifstream file{path, std::ios::in | std::ios::binary};
        if (!file.is_open()) {
            return {};
        }

        const auto content = std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        nlohmann::json data;
        try {
            data = nlohmann::json::parse(content);
        } catch (const nlohmann::json::parse_error&) {
            return {};
        }
        if (!data.is_array()) {
            return {};
        }

        auto jobs = std::vector<CronJob>{};
        for (const auto& value : data) {
            if (value.is_object()) {
                jobs.push_back(value.get<CronJob>());
            }
        }
        return jobs;
    }

    auto save_cron_jobs(const std::vector<CronJob>& jobs) -> absl::Status {
        const auto path = registry_path(true);
        auto data = nlohmann::json::array();
        for (const auto& job : jobs) {
            data.push_back(job);
        }

        std::ofstream file{path, std::ios::out | std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            return absl::PermissionDeniedError(absl::StrCat("failed to open cron registry: ", path.string()));
        }

        file << data.dump(2) << '\n';
        file.close();
        if (file.fail()) {
            return absl::InternalError(absl::StrCat("failed to write cron registry: ", path.string()));
        }
        return absl::OkStatus();
    }

    auto upsert_cron_job(CronJob job) -> absl::Status {
        auto jobs = load_cron_jobs();
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&job](const CronJob& existing) {
                       return existing.name == job.name;
                   }),
                   jobs.end());
        jobs.push_back(std::move(job));
        std::sort(jobs.begin(), jobs.end(), [](const CronJob& lhs, const CronJob& rhs) {
            return lhs.name < rhs.name;
        });
        return save_cron_jobs(jobs);
    }

    auto delete_cron_job(const std::string& name) -> absl::StatusOr<bool> {
        auto jobs = load_cron_jobs();
        const auto old_size = jobs.size();
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&name](const CronJob& job) {
                       return job.name == name;
                   }),
                   jobs.end());
        if (jobs.size() == old_size) {
            return false;
        }

        const auto status = save_cron_jobs(jobs);
        if (!status.ok()) {
            return status;
        }
        return true;
    }

}  // namespace codeharness::services::cron

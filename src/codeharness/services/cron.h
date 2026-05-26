#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace codeharness::services::cron {

    struct CronJob {
        std::string name;
        std::string schedule;
        std::string command;
        std::filesystem::path cwd;
    };

    auto to_json(nlohmann::json& value, const CronJob& job) -> void;
    auto from_json(const nlohmann::json& value, CronJob& job) -> void;

    [[nodiscard]] auto load_cron_jobs() -> std::vector<CronJob>;
    [[nodiscard]] auto save_cron_jobs(const std::vector<CronJob>& jobs) -> absl::Status;
    [[nodiscard]] auto upsert_cron_job(CronJob job) -> absl::Status;
    [[nodiscard]] auto delete_cron_job(const std::string& name) -> absl::StatusOr<bool>;

}  // namespace codeharness::services::cron

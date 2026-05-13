#include "codeharness/logging.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <exception>
#include <filesystem>
#include <memory>

namespace codeharness::logging {
    auto initialize_default_logger(const std::filesystem::path& log_file_path,
                                   spdlog::level::level_enum level) -> absl::Status {
        try {
            if (const auto parent = log_file_path.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent);
            }

            auto logger = std::make_shared<spdlog::logger>(
                "codeharness.file",
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.string(), true));
            logger->set_level(level);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
            logger->flush_on(spdlog::level::debug);

            spdlog::set_default_logger(std::move(logger));
            spdlog::set_level(level);
            return absl::OkStatus();
        } catch (const std::exception& error) {
            return absl::InternalError(absl::StrCat("failed to initialize file logger at ",
                                                    log_file_path.string(), ": ", error.what()));
        }
    }
}  // namespace codeharness::logging

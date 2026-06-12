add_library(codeharness_foundation INTERFACE)
target_link_libraries(codeharness_foundation INTERFACE codeharness_options)

codeharness_link_first_existing(codeharness_foundation INTERFACE expected-lite nonstd::expected-lite)
codeharness_link_first_existing(codeharness_foundation INTERFACE date date::date)
codeharness_link_first_existing(codeharness_foundation INTERFACE nlohmann_json nlohmann_json::nlohmann_json)
target_link_libraries(codeharness_foundation INTERFACE spdlog::spdlog)
# abseil for absl::Status / absl::StatusOr (used throughout codebase)
target_link_libraries(codeharness_foundation INTERFACE absl::status absl::statusor)

add_library(codeharness::foundation ALIAS codeharness_foundation)

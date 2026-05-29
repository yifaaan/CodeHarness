#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>

namespace codeharness::state {

struct ThreadMetadata {
    std::string id;
    std::string name;
    std::string model;
    std::string provider;
    std::chrono::system_clock::time_point created_at;
    bool archived{false};
};

struct MessageRecord {
    std::string id;
    std::string thread_id;
    std::string role;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
};

class StateStore {
public:
    explicit StateStore(std::string_view db_path);

    auto save_thread(const ThreadMetadata& thread) -> bool;
    auto get_thread(std::string_view id) -> std::optional<ThreadMetadata>;
    auto list_threads() -> std::vector<ThreadMetadata>;

    auto append_message(const MessageRecord& msg) -> bool;
    auto list_messages(std::string_view thread_id) -> std::vector<MessageRecord>;

private:
    std::string db_path_;
};

} // namespace codeharness::state

#include "state.hpp"

namespace codeharness::state {

StateStore::StateStore(std::string_view db_path) : db_path_(db_path) {}

auto StateStore::save_thread(const ThreadMetadata& thread) -> bool {
    return true;
}

auto StateStore::get_thread(std::string_view id) -> std::optional<ThreadMetadata> {
    return std::nullopt;
}

auto StateStore::list_threads() -> std::vector<ThreadMetadata> {
    return {};
}

auto StateStore::append_message(const MessageRecord& msg) -> bool {
    return true;
}

auto StateStore::list_messages(std::string_view thread_id) -> std::vector<MessageRecord> {
    return {};
}

} // namespace codeharness::state

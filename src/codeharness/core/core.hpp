#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace codeharness::core {

enum class SessionSource {
    New,
    Forked,
    Resumed
};

struct ThreadInfo {
    std::string id;
    std::string model;
    std::string provider;
    std::string cwd;
};

struct JobRecord {
    std::string id;
    std::string status;
};

class Runtime {
public:
    Runtime() = default;
    ~Runtime() = default;

    auto handle_thread(const ThreadInfo& info) -> bool;
    auto handle_prompt(std::string_view prompt) -> std::string;
    auto invoke_tool(std::string_view name, std::string_view args) -> std::string;
};

} // namespace codeharness::core

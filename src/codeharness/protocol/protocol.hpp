#pragma once

#include <string>

namespace codeharness::protocol {

struct ThreadRequest {
    std::string id;
    std::string action;
    std::string payload;
};

struct ThreadResponse {
    bool success{false};
    std::string data;
    std::string error;
};

struct PromptRequest {
    std::string thread_id;
    std::string message;
};

struct PromptResponse {
    std::string content;
};

struct ToolPayload {
    std::string name;
    std::string arguments;
};

struct ToolOutput {
    bool success{false};
    std::string output;
    std::string error;
};

struct EventFrame {
    std::string type;
    std::string data;
};

} // namespace codeharness::protocol

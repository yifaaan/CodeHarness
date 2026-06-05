#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::network
{

struct SseEvent
{
    std::string event;
    std::string data;
};

class SseParser
{
public:
    auto feed(std::string_view chunk) -> std::vector<SseEvent>;
    auto pending() const noexcept -> const std::string& { return buffer_; }

private:
    std::string buffer_;
    std::string current_event_type_;
    std::string current_data_;
};

} // namespace codeharness::network

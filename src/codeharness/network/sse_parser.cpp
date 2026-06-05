#include "codeharness/network/sse_parser.h"

#include <string>

namespace codeharness::network
{

namespace
{

auto trim_trailing_cr(std::string& line) -> void
{
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
}

auto field_value(std::string_view line, std::size_t colon) -> std::string_view
{
    auto value = line.substr(colon + 1);
    if (!value.empty() && value.front() == ' ')
    {
        value.remove_prefix(1);
    }
    return value;
}

} // namespace

auto SseParser::feed(std::string_view chunk) -> std::vector<SseEvent>
{
    buffer_ += chunk;

    std::vector<SseEvent> events;

    while (true)
    {
        auto eol = buffer_.find('\n');
        if (eol == std::string::npos)
        {
            break;
        }

        std::string line{buffer_, 0, eol};
        buffer_.erase(0, eol + 1);
        trim_trailing_cr(line);

        if (line.empty())
        {
            if (!current_event_type_.empty() || !current_data_.empty())
            {
                if (!current_data_.empty() && current_data_.back() == '\n')
                {
                    current_data_.pop_back();
                }

                events.push_back(
                    SseEvent{
                        .event = std::move(current_event_type_),
                        .data = std::move(current_data_),
                    });

                current_event_type_.clear();
                current_data_.clear();
            }
            continue;
        }

        if (line.front() == ':')
        {
            continue;
        }

        auto colon = line.find(':');
        const auto field = colon == std::string::npos ? std::string_view{line} : std::string_view{line}.substr(0, colon);
        const auto value = colon == std::string::npos ? std::string_view{} : field_value(line, colon);

        if (field == "event")
        {
            current_event_type_ = value;
        }
        else if (field == "data")
        {
            current_data_ += value;
            current_data_ += '\n';
        }
    }

    return events;
}

} // namespace codeharness::network

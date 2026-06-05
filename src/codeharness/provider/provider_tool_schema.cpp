#include "codeharness/provider/provider_tool_schema.h"

namespace codeharness
{

auto loose_tool_input_schema() -> nlohmann::json
{
    return {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"additionalProperties", true},
    };
}

auto parse_tool_input_json_or_empty_object(std::string_view value) -> nlohmann::json
{
    if (value.empty())
    {
        return nlohmann::json::object();
    }

    try
    {
        return nlohmann::json::parse(value);
    }
    catch (const nlohmann::json::exception&)
    {
        return nlohmann::json::object();
    }
}

} // namespace codeharness

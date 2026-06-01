#include "codeharness/tools/tool.h"

#include "codeharness/core/error.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace codeharness
{

auto parse_tool_request_input(ToolRequest& request, std::string_view tool_name) -> Result<nlohmann::json*>
{
    if (!request.parsed_input.is_null())
    {
        return &request.parsed_input;
    }

    try
    {
        request.parsed_input = nlohmann::json::parse(request.input_json);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<nlohmann::json*>(ErrorKind::InvalidArgument,
                                     std::string{tool_name} + " input is not valid JSON: " + error.what());
    }

    return &request.parsed_input;
}

} // namespace codeharness

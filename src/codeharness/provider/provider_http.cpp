#include "codeharness/provider/provider_http.h"

#include <nlohmann/json.hpp>

namespace codeharness
{

auto provider_endpoint_url(std::string base_url,
                           std::string_view endpoint,
                           std::string_view default_url) -> std::string
{
    if (base_url.empty())
    {
        return std::string{default_url};
    }

    if (base_url.ends_with(endpoint))
    {
        return base_url;
    }

    if (!base_url.ends_with('/'))
    {
        base_url += '/';
    }
    base_url += endpoint;
    return base_url;
}

auto provider_http_error_message(std::string_view provider_name,
                                 const network::HttpResponse& response) -> std::string
{
    auto error = std::string{provider_name} + " API returned status " + std::to_string(response.status_code);
    if (response.body.empty())
    {
        return error;
    }

    try
    {
        const auto json = nlohmann::json::parse(response.body);
        if (const auto found = json.find("error"); found != json.end() && found->is_object())
        {
            if (const auto message = found->find("message"); message != found->end() && message->is_string())
            {
                error += ": ";
                error += message->get<std::string>();
                return error;
            }
        }
    }
    catch (const nlohmann::json::exception&)
    {
    }

    error += ": ";
    error += response.body;
    return error;
}

} // namespace codeharness

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

    // If the base URL already ends with the endpoint, use it as-is.
    if (base_url.ends_with(endpoint))
    {
        return base_url;
    }

    // Extract the path prefix from the default URL that sits between the
    // host and the endpoint name, e.g. "/v1" from
    //   "https://api.openai.com/v1/responses" / endpoint "responses".
    // When the custom base URL has no path component, we insert this
    // prefix so that "https://custom.host" becomes
    // "https://custom.host/v1/responses" instead of
    // "https://custom.host/responses".
    std::string_view prefix;
    auto endpoint_pos = default_url.find(endpoint);
    if (endpoint_pos != std::string_view::npos && endpoint_pos > 0)
    {
        // Find the '/' before the endpoint name, then find the '/' before that.
        auto before_endpoint = endpoint_pos - 1;
        if (default_url[before_endpoint] == '/')
        {
            --before_endpoint;
        }
        auto slash_pos = default_url.rfind('/', before_endpoint);
        if (slash_pos != std::string_view::npos)
        {
            prefix = default_url.substr(slash_pos, endpoint_pos - slash_pos);
        }
    }

    if (!prefix.empty())
    {
        // Check if base_url already has a meaningful path component
        // (anything more than just a trailing slash).
        auto scheme_end = base_url.find("://");
        auto has_path = false;
        if (scheme_end != std::string::npos)
        {
            auto first_slash = base_url.find('/', scheme_end + 3);
            if (first_slash != std::string::npos)
            {
                has_path = (first_slash + 1 < base_url.size());
            }
        }

        if (!has_path)
        {
            if (base_url.back() == '/')
            {
                base_url.pop_back();
            }
            base_url += std::string{prefix};
        }
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

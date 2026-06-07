#include "codeharness/provider/provider_http.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <random>
#include <thread>

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

auto should_retry_http_status(int status_code) -> bool
{
    // 429 (rate limit) and 5xx (server errors) are transient.
    return status_code == 429 || (status_code >= 500 && status_code < 600);
}

auto provider_retry_delay(int attempt,
                          std::chrono::milliseconds base_delay,
                          std::chrono::milliseconds max_delay) -> std::chrono::milliseconds
{
    // Exponential backoff: base_delay * 2^attempt, capped at max_delay.
    auto delay = base_delay;
    for (int i = 0; i < attempt; ++i)
    {
        delay *= 2;
        if (delay >= max_delay)
        {
            delay = max_delay;
            break;
        }
    }

    // Add jitter: ±25%
    static thread_local std::mt19937 rng{std::random_device{}()};
    auto jitter_range = delay / 4;
    std::uniform_int_distribution<long long> jitter(-jitter_range.count(), jitter_range.count());
    delay += std::chrono::milliseconds{jitter(rng)};

    if (delay < std::chrono::milliseconds::zero())
    {
        delay = base_delay;
    }
    if (delay > max_delay)
    {
        delay = max_delay;
    }

    return delay;
}

auto provider_http_post_with_retry(
    std::string_view provider_name,
    const std::function<Result<network::HttpResponse>()>& request_fn,
    int max_retries) -> Result<network::HttpResponse>
{
    for (int attempt = 1; attempt <= max_retries; ++attempt)
    {
        auto response = request_fn();

        // Network-level failure (connection refused, DNS, TLS, etc.)
        if (!response)
        {
            if (attempt < max_retries)
            {
                auto delay = provider_retry_delay(attempt - 1);
                spdlog::debug("{} attempt {}/{} failed (network), retrying in {}ms: {}",
                              provider_name, attempt, max_retries, delay.count(), response.error().message);
                std::this_thread::sleep_for(delay);
                continue;
            }
            return response;
        }

        // Successful response
        if (response->status_code == 200)
        {
            return response;
        }

        // Transient server error – retry
        if (should_retry_http_status(response->status_code) && attempt < max_retries)
        {
            auto delay = provider_retry_delay(attempt - 1);
            spdlog::debug("{} attempt {}/{} returned {} (transient), retrying in {}ms",
                          provider_name, attempt, max_retries, response->status_code, delay.count());
            std::this_thread::sleep_for(delay);
            continue;
        }

        // Non-retryable or last attempt — return as-is.
        return response;
    }

    // Unreachable: the loop always returns on attempt == max_retries.
    return fail<network::HttpResponse>(ErrorKind::Provider,
                                       std::string{provider_name} + " exhausted retries");
}

} // namespace codeharness

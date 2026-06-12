#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "codeharness/network/http_client.h"

namespace codeharness {

auto provider_endpoint_url(std::string base_url, std::string_view endpoint, std::string_view default_url)
    -> std::string;

auto provider_http_error_message(std::string_view provider_name, const network::HttpResponse& response) -> std::string;

/// Returns true when the HTTP status code represents a transient failure
/// that is safe to retry (rate limit, server errors).
auto should_retry_http_status(int status_code) -> bool;

/// Exponential-backoff delay for a given retry attempt (0-based).
auto provider_retry_delay(int attempt, std::chrono::milliseconds base_delay = std::chrono::seconds{1},
                          std::chrono::milliseconds max_delay = std::chrono::seconds{30}) -> std::chrono::milliseconds;

/// Make an HTTP POST with retry for transient failures.
///
/// `request_fn` is called fresh for each attempt — the provider should
/// reset any stream-parser state inside it.  Returns the first successful
/// response (status 200), or the last error / non-retryable response.
auto provider_http_post_with_retry(std::string_view provider_name,
                                   const std::function<absl::StatusOr<network::HttpResponse>()>& request_fn,
                                   int max_retries = 3) -> absl::StatusOr<network::HttpResponse>;

}  // namespace codeharness

#pragma once

#include "codeharness/network/http_client.h"

#include <string>
#include <string_view>

namespace codeharness
{

auto provider_endpoint_url(std::string base_url,
                           std::string_view endpoint,
                           std::string_view default_url) -> std::string;

auto provider_http_error_message(std::string_view provider_name,
                                 const network::HttpResponse& response) -> std::string;

} // namespace codeharness

#pragma once

#include "codeharness/core/error.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace codeharness::network
{

struct HttpResponse
{
    int status_code = 0;
    std::string status_text;
    std::map<std::string, std::string> headers;
    std::string body;
};

// Return false to stop reading the response body after the current chunk.
using OnChunk = std::function<bool(std::string_view)>;

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    auto operator=(const HttpClient&) -> HttpClient& = delete;

    HttpClient(HttpClient&&) = delete;
    auto operator=(HttpClient&&) -> HttpClient& = delete;

    auto post(std::string_view url_str,
              const std::map<std::string, std::string>& headers,
              std::string_view body,
              const OnChunk& on_chunk = {}) -> absl::StatusOr<HttpResponse>;

    auto get(std::string_view url_str,
             const std::map<std::string, std::string>& headers,
             const OnChunk& on_chunk = {}) -> absl::StatusOr<HttpResponse>;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace codeharness::network

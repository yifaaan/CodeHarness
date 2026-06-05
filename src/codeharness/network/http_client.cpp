#include "codeharness/network/http_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace codeharness::network
{

namespace
{

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;
using Tcp = asio::ip::tcp;

struct ParsedUrl
{
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;
};

auto lower_copy(std::string value) -> std::string
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto parse_http_url(std::string_view raw) -> Result<ParsedUrl>
{
    auto parsed = boost::urls::parse_uri(raw);
    if (!parsed)
    {
        return fail<ParsedUrl>(ErrorKind::InvalidArgument, "invalid URL: " + std::string{raw});
    }

    const auto view = *parsed;
    auto scheme = lower_copy(std::string{view.scheme()});
    if (scheme != "http" && scheme != "https")
    {
        return fail<ParsedUrl>(ErrorKind::InvalidArgument, "unsupported URL scheme: " + scheme);
    }

    auto host = std::string{view.host()};
    if (host.empty())
    {
        return fail<ParsedUrl>(ErrorKind::InvalidArgument, "URL requires a host");
    }

    std::string target;
    if (view.encoded_target().empty())
    {
        target = "/";
    }
    else
    {
        target = std::string{view.encoded_target()};
    }

    auto port = std::string{view.port()};
    if (port.empty())
    {
        port = scheme == "https" ? "443" : "80";
    }

    return ParsedUrl{
        .scheme = std::move(scheme),
        .host = std::move(host),
        .port = std::move(port),
        .target = std::move(target),
    };
}

template <typename Body>
auto response_to_http_response(const http::response<Body>& response, std::string body) -> HttpResponse
{
    HttpResponse result;
    result.status_code = static_cast<int>(response.result_int());
    result.status_text = std::string{response.reason()};
    result.body = std::move(body);

    for (const auto& field : response.base())
    {
        result.headers[lower_copy(std::string{field.name_string()})] = std::string{field.value()};
    }

    return result;
}

auto make_request(http::verb method,
                  const ParsedUrl& url,
                  const std::map<std::string, std::string>& headers,
                  std::string_view body) -> http::request<http::string_body>
{
    http::request<http::string_body> request{method, url.target, 11};
    request.set(http::field::host, url.host);
    request.set(http::field::user_agent, "CodeHarness/0.1");

    for (const auto& [key, value] : headers)
    {
        request.set(key, value);
    }

    if (method == http::verb::post)
    {
        request.body() = std::string{body};
        request.prepare_payload();
    }

    return request;
}

template <typename Stream>
auto read_response(Stream& stream, beast::flat_buffer& buffer, const OnChunk& on_chunk, std::string_view label)
    -> Result<HttpResponse>
{
    beast::error_code error;
    http::response_parser<http::buffer_body> parser;
    parser.body_limit(std::numeric_limits<std::uint64_t>::max());

    http::read_header(stream, buffer, parser, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, std::string{label} + " read headers failed: " + error.message());
    }

    std::string body;
    std::array<char, 16 * 1024> chunk_buffer{};
    while (!parser.is_done())
    {
        parser.get().body().data = chunk_buffer.data();
        parser.get().body().size = chunk_buffer.size();

        error = {};
        http::read(stream, buffer, parser, error);

        const auto bytes_read = chunk_buffer.size() - parser.get().body().size;
        if (bytes_read > 0)
        {
            const std::string_view chunk{chunk_buffer.data(), bytes_read};
            body.append(chunk);
            if (on_chunk)
            {
                on_chunk(chunk);
            }
        }

        if (error == http::error::need_buffer)
        {
            continue;
        }

        if (error)
        {
            return fail<HttpResponse>(ErrorKind::Network, std::string{label} + " read body failed: " + error.message());
        }
    }

    return response_to_http_response(parser.get(), std::move(body));
}

auto run_plain_request(asio::io_context& io,
                       const ParsedUrl& url,
                       http::request<http::string_body> request,
                       const OnChunk& on_chunk) -> Result<HttpResponse>
{
    beast::error_code error;
    Tcp::resolver resolver{io};
    beast::tcp_stream stream{io};

    auto endpoints = resolver.resolve(url.host, url.port, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTP resolve failed: " + error.message());
    }

    stream.connect(endpoints, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTP connect failed: " + error.message());
    }

    http::write(stream, request, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTP write failed: " + error.message());
    }

    beast::flat_buffer buffer;
    auto response = read_response(stream, buffer, on_chunk, "HTTP");

    stream.socket().shutdown(Tcp::socket::shutdown_both, error);
    return response;
}

auto run_tls_request(asio::io_context& io,
                     ssl::context& tls,
                     const ParsedUrl& url,
                     http::request<http::string_body> request,
                     const OnChunk& on_chunk) -> Result<HttpResponse>
{
    beast::error_code error;
    Tcp::resolver resolver{io};
    beast::ssl_stream<beast::tcp_stream> stream{io, tls};

    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str()))
    {
        return fail<HttpResponse>(ErrorKind::Network, "failed to set TLS SNI host");
    }

    auto endpoints = resolver.resolve(url.host, url.port, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTPS resolve failed: " + error.message());
    }

    beast::get_lowest_layer(stream).connect(endpoints, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTPS connect failed: " + error.message());
    }

    stream.handshake(ssl::stream_base::client, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTPS handshake failed: " + error.message());
    }

    http::write(stream, request, error);
    if (error)
    {
        return fail<HttpResponse>(ErrorKind::Network, "HTTPS write failed: " + error.message());
    }

    beast::flat_buffer buffer;
    auto response = read_response(stream, buffer, on_chunk, "HTTPS");

    stream.shutdown(error);
    if (error == asio::error::eof || error == ssl::error::stream_truncated)
    {
        error = {};
    }

    return response;
}

} // namespace

namespace
{

auto do_request(asio::io_context& io,
                ssl::context& tls,
                http::verb method,
                std::string_view url_str,
                const std::map<std::string, std::string>& headers,
                std::string_view body,
                const OnChunk& on_chunk) -> Result<HttpResponse>
{
    auto parsed = parse_http_url(url_str);
    if (!parsed)
    {
        return nonstd::make_unexpected(parsed.error());
    }

    auto request = make_request(method, *parsed, headers, body);
    io.restart();
    if (parsed->scheme == "https")
    {
        return run_tls_request(io, tls, *parsed, std::move(request), on_chunk);
    }

    return run_plain_request(io, *parsed, std::move(request), on_chunk);
}

} // namespace

struct HttpClient::Impl
{
    asio::io_context io;
    ssl::context tls{ssl::context::tls_client};

    Impl()
    {
        tls.set_default_verify_paths();
        tls.set_verify_mode(ssl::verify_peer);
    }
};

HttpClient::HttpClient() : impl_{std::make_unique<Impl>()} {}

HttpClient::~HttpClient() = default;

auto HttpClient::post(std::string_view url_str,
                      const std::map<std::string, std::string>& headers,
                      std::string_view body,
                      const OnChunk& on_chunk) -> Result<HttpResponse>
{
    return do_request(impl_->io, impl_->tls, http::verb::post, url_str, headers, body, on_chunk);
}

auto HttpClient::get(std::string_view url_str,
                     const std::map<std::string, std::string>& headers,
                     const OnChunk& on_chunk) -> Result<HttpResponse>
{
    return do_request(impl_->io, impl_->tls, http::verb::get, url_str, headers, {}, on_chunk);
}

} // namespace codeharness::network

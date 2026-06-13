#include "beast_http_client.h"

#include <absl/status/status.h>
#include <fmt/format.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <string>
#include <utility>

namespace codeharness::llm {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

namespace {

constexpr auto kConnectTimeout = std::chrono::seconds(30);
constexpr auto kReadTimeout = std::chrono::seconds(120);
constexpr auto kShutdownTimeout = std::chrono::seconds(5);

absl::Status ErrorCodeToStatus(beast::error_code ec, std::string_view context) {
  if (ec == asio::error::host_not_found || ec == asio::error::service_not_found) {
    return absl::NotFoundError(fmt::format("{}: {}", context, ec.message()));
  }
  if (ec == asio::error::connection_refused || ec == asio::error::connection_reset || ec == asio::error::eof ||
      ec == ssl::error::stream_truncated) {
    return absl::UnavailableError(fmt::format("{}: {}", context, ec.message()));
  }
  return absl::InternalError(fmt::format("{}: {}", context, ec.message()));
}

template <typename Stream>
absl::StatusOr<HttpResponse> DoStreamRequest(Stream& stream, const HttpRequest& req,
                                             const StreamChunkCallback& on_chunk, std::stop_token stop_token) {
  beast::error_code ec;

  http::request<http::string_body> http_req;
  http_req.method_string(req.method);
  http_req.target(req.path);
  http_req.version(11);
  http_req.set(http::field::host, req.host);
  http_req.set(http::field::user_agent, "CodeHarness/0.1");
  for (const auto& [key, value] : req.headers) {
    http_req.set(key, value);
  }
  http_req.body() = req.body;
  http_req.prepare_payload();

  beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
  http::write(stream, http_req, ec);
  if (ec) return ErrorCodeToStatus(ec, "failed to write request");

  beast::flat_buffer buffer;
  http::response_parser<http::buffer_body> parser;
  parser.body_limit(boost::none);
  parser.header_limit(16 * 1024);

  beast::get_lowest_layer(stream).expires_after(kReadTimeout);
  http::read_header(stream, buffer, parser, ec);
  if (ec) return ErrorCodeToStatus(ec, "failed to read response headers");

  int status = parser.get().result_int();
  auto& body = parser.get().body();

  if (status != 200) {
    char err_buf[4096];
    std::string err_body;
    while (!parser.is_done()) {
      body.data = err_buf;
      body.size = sizeof(err_buf);
      beast::error_code read_ec;
      http::read_some(stream, buffer, parser, read_ec);
      auto n = sizeof(err_buf) - body.size;
      if (n > 0) err_body.append(err_buf, n);
      if (read_ec == http::error::need_buffer) continue;
      if (read_ec == http::error::end_of_stream) break;
      if (read_ec == ssl::error::stream_truncated) break;
      if (read_ec) break;
    }
    return HttpResponse{status, {}, std::move(err_body)};
  }

  char chunk_buf[8192];
  while (!parser.is_done()) {
    if (stop_token.stop_requested()) {
      beast::get_lowest_layer(stream).cancel();
      return absl::CancelledError("request cancelled");
    }

    body.data = chunk_buf;
    body.size = sizeof(chunk_buf);

    beast::error_code read_ec;
    http::read_some(stream, buffer, parser, read_ec);

    auto bytes_read = sizeof(chunk_buf) - body.size;
    if (bytes_read > 0) {
      if (!on_chunk(std::string_view(chunk_buf, bytes_read))) break;
    }

    if (read_ec == http::error::need_buffer) continue;
    if (read_ec == http::error::end_of_stream) break;
    if (read_ec == ssl::error::stream_truncated) break;
    if (read_ec) return ErrorCodeToStatus(read_ec, "read error");
  }

  beast::get_lowest_layer(stream).expires_after(kShutdownTimeout);
  beast::error_code shutdown_ec;
  stream.shutdown(shutdown_ec);
  if (shutdown_ec && shutdown_ec != ssl::error::stream_truncated && shutdown_ec != beast::errc::not_connected) {
    spdlog::debug("TLS shutdown: {}", shutdown_ec.message());
  }

  return HttpResponse{status, {}, ""};
}

}  // namespace

BeastHttpClient::BeastHttpClient() : ssl_ctx_(std::make_unique<ssl::context>(ssl::context::tlsv12_client)) {
  ssl_ctx_->set_default_verify_paths();
  ssl_ctx_->set_verify_mode(ssl::verify_peer);
}

BeastHttpClient::~BeastHttpClient() = default;

absl::StatusOr<HttpResponse> BeastHttpClient::Request(const HttpRequest& req) {
  std::string body;
  auto result = StreamRequest(req, [&body](std::string_view chunk) {
    body.append(chunk);
    return true;
  });
  if (!result.ok()) return result.status();
  auto resp = std::move(*result);
  resp.body = std::move(body);
  return resp;
}

absl::StatusOr<HttpResponse> BeastHttpClient::StreamRequest(const HttpRequest& req, const StreamChunkCallback& on_chunk,
                                                            std::stop_token stop_token) {
  if (!req.use_tls) {
    return absl::UnimplementedError("plain HTTP not yet supported, use TLS");
  }

  asio::io_context ioc;
  ssl::stream<beast::tcp_stream> stream(ioc, *ssl_ctx_);

  if (!SSL_set_tlsext_host_name(stream.native_handle(), req.host.c_str())) {
    return absl::InternalError("failed to set SNI hostname");
  }

  beast::error_code ec;
  tcp::resolver resolver(ioc);
  auto endpoints = resolver.resolve(req.host, req.port, ec);
  if (ec) {
    return ErrorCodeToStatus(ec, fmt::format("failed to resolve '{}'", req.host));
  }

  beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
  beast::get_lowest_layer(stream).connect(endpoints, ec);
  if (ec) {
    return ErrorCodeToStatus(ec, fmt::format("failed to connect to '{}:{}'", req.host, req.port));
  }

  beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
  stream.handshake(ssl::stream_base::client, ec);
  if (ec) {
    return ErrorCodeToStatus(ec, "TLS handshake failed");
  }

  return DoStreamRequest(stream, req, on_chunk, stop_token);
}

}  // namespace codeharness::llm

#include "BeastHttpClient.h"

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

namespace codeharness::llm
{

	namespace beast = boost::beast;
	namespace http = beast::http;
	namespace asio = boost::asio;
	namespace ssl = asio::ssl;
	using tcp = asio::ip::tcp;

	namespace
	{

		constexpr auto kConnectTimeout = std::chrono::seconds(30);
		constexpr auto kReadTimeout = std::chrono::seconds(120);
		constexpr auto kShutdownTimeout = std::chrono::seconds(5);

		absl::Status ErrorCodeToStatus(beast::error_code ec, std::string_view context)
		{
			if (ec == asio::error::host_not_found || ec == asio::error::service_not_found)
			{
				return absl::NotFoundError(fmt::format("{}: {}", context, ec.message()));
			}
			if (ec == asio::error::connection_refused || ec == asio::error::connection_reset || ec == asio::error::eof ||
				ec == ssl::error::stream_truncated)
			{
				return absl::UnavailableError(fmt::format("{}: {}", context, ec.message()));
			}
			return absl::InternalError(fmt::format("{}: {}", context, ec.message()));
		}

		template <typename Stream>
		absl::StatusOr<HttpResponse> DoStreamRequest(Stream& stream, const HttpRequest& req, const StreamChunkCallback& onChunk, std::stop_token stopToken)
		{
			beast::error_code ec;

			http::request<http::string_body> httpReq;
			httpReq.method_string(req.method);
			httpReq.target(req.path);
			httpReq.version(11);
			httpReq.set(http::field::host, req.host);
			httpReq.set(http::field::user_agent, "CodeHarness/0.1");
			for (const auto& [key, value] : req.headers)
			{
				httpReq.set(key, value);
			}
			httpReq.body() = req.body;
			httpReq.prepare_payload();

			beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
			http::write(stream, httpReq, ec);
			if (ec)
				return ErrorCodeToStatus(ec, "failed to write request");

			beast::flat_buffer buffer;
			http::response_parser<http::buffer_body> parser;
			parser.body_limit(boost::none);
			parser.header_limit(16 * 1024);

			beast::get_lowest_layer(stream).expires_after(kReadTimeout);
			http::read_header(stream, buffer, parser, ec);
			if (ec)
				return ErrorCodeToStatus(ec, "failed to read response headers");

			int status = parser.get().result_int();
			auto& body = parser.get().body();

			if (status != 200)
			{
				char errBuf[4096];
				std::string errBody;
				while (!parser.is_done())
				{
					body.data = errBuf;
					body.size = sizeof(errBuf);
					beast::error_code readEc;
					http::read_some(stream, buffer, parser, readEc);
					auto n = sizeof(errBuf) - body.size;
					if (n > 0)
						errBody.append(errBuf, n);
					if (readEc == http::error::need_buffer)
						continue;
					if (readEc == http::error::end_of_stream)
						break;
					if (readEc == ssl::error::stream_truncated)
						break;
					if (readEc)
						break;
				}
				return HttpResponse{status, {}, std::move(errBody)};
			}

			char chunkBuf[8192];
			while (!parser.is_done())
			{
				if (stopToken.stop_requested())
				{
					beast::get_lowest_layer(stream).cancel();
					return absl::CancelledError("request cancelled");
				}

				body.data = chunkBuf;
				body.size = sizeof(chunkBuf);

				beast::error_code readEc;
				http::read_some(stream, buffer, parser, readEc);

				auto bytesRead = sizeof(chunkBuf) - body.size;
				if (bytesRead > 0)
				{
					if (!onChunk(std::string_view(chunkBuf, bytesRead)))
						break;
				}

				if (readEc == http::error::need_buffer)
					continue;
				if (readEc == http::error::end_of_stream)
					break;
				if (readEc == ssl::error::stream_truncated)
					break;
				if (readEc)
					return ErrorCodeToStatus(readEc, "read error");
			}

			beast::get_lowest_layer(stream).expires_after(kShutdownTimeout);
			beast::error_code shutdownEc;
			stream.shutdown(shutdownEc);
			if (shutdownEc && shutdownEc != ssl::error::stream_truncated && shutdownEc != beast::errc::not_connected)
			{
				spdlog::debug("TLS shutdown: {}", shutdownEc.message());
			}

			return HttpResponse{status, {}, ""};
		}

	} // namespace

	BeastHttpClient::BeastHttpClient() : sslCtx(std::make_unique<ssl::context>(ssl::context::tlsv12_client))
	{
		sslCtx->set_default_verify_paths();
		sslCtx->set_verify_mode(ssl::verify_peer);
	}

	BeastHttpClient::~BeastHttpClient() = default;

	absl::StatusOr<HttpResponse> BeastHttpClient::Request(const HttpRequest& req)
	{
		std::string body;
		auto result = StreamRequest(req, [&body](std::string_view chunk) {
			body.append(chunk);
			return true;
		});
		if (!result.ok())
			return result.status();
		auto resp = std::move(*result);
		resp.body = std::move(body);
		return resp;
	}

	absl::StatusOr<HttpResponse> BeastHttpClient::StreamRequest(const HttpRequest& req, const StreamChunkCallback& onChunk, std::stop_token stopToken)
	{
		if (!req.useTls)
		{
			return absl::UnimplementedError("plain HTTP not yet supported, use TLS");
		}

		asio::io_context ioc;
		ssl::stream<beast::tcp_stream> stream(ioc, *sslCtx);

		if (!SSL_set_tlsext_host_name(stream.native_handle(), req.host.c_str()))
		{
			return absl::InternalError("failed to set SNI hostname");
		}

		beast::error_code ec;
		tcp::resolver resolver(ioc);
		auto endpoints = resolver.resolve(req.host, req.port, ec);
		if (ec)
		{
			return ErrorCodeToStatus(ec, fmt::format("failed to resolve '{}'", req.host));
		}

		beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
		beast::get_lowest_layer(stream).connect(endpoints, ec);
		if (ec)
		{
			return ErrorCodeToStatus(ec, fmt::format("failed to connect to '{}:{}'", req.host, req.port));
		}

		beast::get_lowest_layer(stream).expires_after(kConnectTimeout);
		stream.handshake(ssl::stream_base::client, ec);
		if (ec)
		{
			return ErrorCodeToStatus(ec, "TLS handshake failed");
		}

		return DoStreamRequest(stream, req, onChunk, stopToken);
	}

} // namespace codeharness::llm

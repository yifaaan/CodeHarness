#pragma once

#include <memory>

#include "http_client.h"

namespace boost::asio
{
namespace ssl
{
class context;
}
} // namespace boost::asio

namespace codeharness::llm
{

class BeastHttpClient : public HttpClient
{
public:
	BeastHttpClient();
	~BeastHttpClient() override;

	BeastHttpClient(const BeastHttpClient&) = delete;
	BeastHttpClient& operator=(const BeastHttpClient&) = delete;

	absl::StatusOr<HttpResponse> Request(const HttpRequest& req) override;
	absl::StatusOr<HttpResponse> StreamRequest(const HttpRequest& req, const StreamChunkCallback& onChunk, std::stop_token stopToken = {}) override;

private:
	std::unique_ptr<boost::asio::ssl::context> sslCtx;
};

} // namespace codeharness::llm

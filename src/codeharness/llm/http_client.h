#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <functional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace codeharness::llm
{

struct HttpRequest
{
	std::string host;
	std::string port = "443";
	std::string path = "/";
	std::string method = "POST";
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	bool useTls = true;
};

struct HttpResponse
{
	int status = 0;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
};

using StreamChunkCallback = std::function<bool(std::string_view)>;

class HttpClient
{
public:
	virtual ~HttpClient() = default;

	virtual absl::StatusOr<HttpResponse> Request(const HttpRequest& req) = 0;

	virtual absl::StatusOr<HttpResponse> StreamRequest(const HttpRequest& req, const StreamChunkCallback& onChunk, std::stop_token stopToken = {}) = 0;
};

} // namespace codeharness::llm

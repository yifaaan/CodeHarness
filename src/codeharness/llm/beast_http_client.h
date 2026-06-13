#pragma once

#include <memory>

#include "http_client.h"

namespace boost::asio {
namespace ssl {
class context;
}
}  // namespace boost::asio

namespace codeharness::llm {

class BeastHttpClient : public HttpClient {
 public:
  BeastHttpClient();
  ~BeastHttpClient() override;

  BeastHttpClient(const BeastHttpClient&) = delete;
  BeastHttpClient& operator=(const BeastHttpClient&) = delete;

  absl::StatusOr<HttpResponse> Request(const HttpRequest& req) override;
  absl::StatusOr<HttpResponse> StreamRequest(const HttpRequest& req, const StreamChunkCallback& on_chunk,
                                             std::stop_token stop_token = {}) override;

 private:
  std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;
};

}  // namespace codeharness::llm

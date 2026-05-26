#pragma once

#include <doctest/doctest.h>
#include <httplib.h>

#include <string>
#include <thread>
#include <utility>

namespace codeharness::tests {

    class LocalHttpServer {
    public:
        explicit LocalHttpServer(std::string body) {
            server_.Get("/", [body = std::move(body)](const httplib::Request&,
                                                       httplib::Response& response) {
                response.set_content(body, "text/html; charset=utf-8");
            });
            port_ = server_.bind_to_any_port("127.0.0.1");
            REQUIRE(port_ > 0);

            thread_ = std::jthread{[this] { static_cast<void>(server_.listen_after_bind()); }};
            server_.wait_until_ready();
        }

        ~LocalHttpServer() {
            server_.stop();
        }

        LocalHttpServer(const LocalHttpServer&) = delete;
        auto operator=(const LocalHttpServer&) -> LocalHttpServer& = delete;

        [[nodiscard]] auto url() const -> std::string {
            return "http://127.0.0.1:" + std::to_string(port_) + "/";
        }

    private:
        httplib::Server server_;
        int port_{0};
        std::jthread thread_;
    };

}  // namespace codeharness::tests

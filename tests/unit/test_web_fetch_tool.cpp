#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "codeharness/tools/web_fetch_tool.h"
#include "local_http_server.h"

using namespace codeharness;

TEST_CASE("web fetch tool reads html from a local server") {
    auto server =
        tests::LocalHttpServer{"<html><body><h1>CodeHarness Test</h1><p>web fetch works</p>"
                               "<script>hidden()</script></body></html>"};

    auto tool = tools::WebFetchTool{};
    const auto result = tool.execute(
        nlohmann::json{{"url", server.url()}},
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(result->find("Status: 200") != std::string::npos);
    CHECK(result->find("Content-Type: text/html") != std::string::npos);
    CHECK(result->find("CodeHarness Test") != std::string::npos);
    CHECK(result->find("web fetch works") != std::string::npos);
    CHECK(result->find("hidden") == std::string::npos);
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("web fetch tool validates inputs") {
    auto tool = tools::WebFetchTool{};
    const auto context = tools::ToolExecutionContext{.cwd = std::filesystem::current_path()};

    const auto missing_scheme =
        tool.execute(nlohmann::json{{"url", "example.com"}}, context);
    REQUIRE_FALSE(missing_scheme.ok());
    CHECK(missing_scheme.status().message() == "url must start with http:// or https://");

    const auto bad_limit =
        tool.execute(nlohmann::json{{"url", "https://example.com"}, {"max_chars", 100}},
                     context);
    REQUIRE_FALSE(bad_limit.ok());
    CHECK(bad_limit.status().message() == "max_chars must be between 500 and 50000");
}

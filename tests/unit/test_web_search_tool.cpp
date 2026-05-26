#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "codeharness/tools/web_search_tool.h"
#include "local_http_server.h"

using namespace codeharness;

TEST_CASE("web search tool reads results from a local search endpoint") {
    auto server = tests::LocalHttpServer{
        "<html><body>"
        "<a class=\"result__a\" href=\"https://example.com/docs\">OpenHarness Docs</a>"
        "<div class=\"result__snippet\">Search query was openharness docs and docs were "
        "found.</div>"
        "</body></html>"};

    auto tool = tools::WebSearchTool{};
    const auto result =
        tool.execute(nlohmann::json{
                         {"query", "openharness docs"},
                         {"search_url", server.url()},
                     },
                     tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(result->find("Search results for: openharness docs") != std::string::npos);
    CHECK(result->find("1. OpenHarness Docs") != std::string::npos);
    CHECK(result->find("URL: https://example.com/docs") != std::string::npos);
    CHECK(result->find("Search query was openharness docs") != std::string::npos);
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("web search tool decodes DuckDuckGo redirect URLs") {
    auto server = tests::LocalHttpServer{
        "<html><body>"
        "<a class=\"result__a\" "
        "href=\"https://duckduckgo.com/l/?kh=-1&amp;uddg=https%3A%2F%2Fexample.com%2Ftarget\">"
        "Redirected Result</a>"
        "<div class=\"result__snippet\">A redirected result.</div>"
        "</body></html>"};

    auto tool = tools::WebSearchTool{};
    const auto result =
        tool.execute(nlohmann::json{
                         {"query", "redirect"},
                         {"search_url", server.url()},
                     },
                     tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(result->find("URL: https://example.com/target") != std::string::npos);
}

TEST_CASE("web search tool validates inputs") {
    auto tool = tools::WebSearchTool{};
    const auto context = tools::ToolExecutionContext{.cwd = std::filesystem::current_path()};

    const auto empty_query = tool.execute(nlohmann::json{{"query", ""}}, context);
    REQUIRE_FALSE(empty_query.ok());
    CHECK(empty_query.status().message() == "query must not be empty");

    const auto bad_limit =
        tool.execute(nlohmann::json{{"query", "openharness"}, {"max_results", 0}}, context);
    REQUIRE_FALSE(bad_limit.ok());
    CHECK(bad_limit.status().message() == "max_results must be between 1 and 10");

    const auto bad_url =
        tool.execute(nlohmann::json{{"query", "openharness"}, {"search_url", "file://search"}},
                     context);
    REQUIRE_FALSE(bad_url.ok());
    CHECK(bad_url.status().message() == "search_url must start with http:// or https://");
}

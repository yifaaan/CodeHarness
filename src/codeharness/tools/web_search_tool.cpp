#include "codeharness/tools/web_search_tool.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <ada.h>
#include <cpr/cpr.h>

#include <chrono>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "codeharness/logging.h"
#include "codeharness/tools/web_common.h"

namespace codeharness::tools {
namespace {

    constexpr int default_max_results = 5;
    constexpr int min_max_results = 1;
    constexpr int max_max_results = 10;
    constexpr auto request_timeout = std::chrono::seconds{20};
    constexpr absl::string_view default_search_url = "https://html.duckduckgo.com/html/";

    struct SearchResult {
        std::string title;
        std::string url;
        std::string snippet;
    };

    [[nodiscard]] auto clean_html(std::string fragment) -> std::string {
        auto text = std::regex_replace(fragment, std::regex{R"(<[^>]+>)"}, " ");
        text = web::decode_html_entities(std::move(text));
        text = std::regex_replace(text, std::regex{R"(\s+)"}, " ");
        absl::StripAsciiWhitespace(&text);
        return text;
    }

    [[nodiscard]] auto normalize_result_url(std::string raw_url) -> std::string {
        raw_url = web::decode_html_entities(std::move(raw_url));

        const auto base = ada::parse("https://duckduckgo.com/");
        const auto parsed = raw_url.starts_with('/') && base
                                ? ada::parse(raw_url, &*base)
                                : ada::parse(raw_url);
        if (!parsed) {
            return raw_url;
        }

        if (absl::EndsWith(parsed->get_hostname(), "duckduckgo.com") &&
            absl::StartsWith(parsed->get_pathname(), "/l/")) {
            auto params = ada::url_search_params{parsed->get_search()};
            const auto target = params.get("uddg");
            if (target.has_value() && !target->empty()) {
                return std::string{*target};
            }
        }

        return std::string{parsed->get_href()};
    }

    [[nodiscard]] auto parse_search_results(const std::string& body, int limit)
        -> std::vector<SearchResult> {
        auto snippets = std::vector<std::string>{};
        const auto snippet_pattern = std::regex{
            R"(<(?:a|div|span)[^>]+class\s*=\s*["'][^"']*(?:result__snippet|result-snippet)[^"']*["'][^>]*>([\s\S]*?)</(?:a|div|span)>)",
            std::regex_constants::icase};

        for (auto it = std::sregex_iterator{body.begin(), body.end(), snippet_pattern};
             it != std::sregex_iterator{}; ++it) {
            snippets.push_back(clean_html((*it)[1].str()));
        }

        auto results = std::vector<SearchResult>{};
        const auto anchor_pattern =
            std::regex{R"(<a([^>]*)>([\s\S]*?)</a>)", std::regex_constants::icase};
        const auto class_pattern =
            std::regex{R"(class\s*=\s*["']([^"']+)["'])", std::regex_constants::icase};
        const auto href_pattern =
            std::regex{R"(href\s*=\s*["']([^"']+)["'])", std::regex_constants::icase};

        for (auto it = std::sregex_iterator{body.begin(), body.end(), anchor_pattern};
             it != std::sregex_iterator{}; ++it) {
            const auto attrs = (*it)[1].str();

            auto class_match = std::smatch{};
            if (!std::regex_search(attrs, class_match, class_pattern)) {
                continue;
            }

            const auto class_names = class_match[1].str();
            if (!absl::StrContains(class_names, "result__a") &&
                !absl::StrContains(class_names, "result-link")) {
                continue;
            }

            auto href_match = std::smatch{};
            if (!std::regex_search(attrs, href_match, href_pattern)) {
                continue;
            }

            const auto snippet_index = results.size();
            auto result = SearchResult{
                .title = clean_html((*it)[2].str()),
                .url = normalize_result_url(href_match[1].str()),
                .snippet = snippet_index < snippets.size() ? snippets[snippet_index] : std::string{},
            };
            if (!result.title.empty() && !result.url.empty()) {
                results.push_back(std::move(result));
            }
            if (results.size() >= static_cast<std::size_t>(limit)) {
                break;
            }
        }

        return results;
    }

    [[nodiscard]] auto fetch_search_page(const std::string& endpoint,
                                         const std::string& query)
        -> absl::StatusOr<std::string> {
        const auto response =
            cpr::Get(cpr::Url{endpoint}, cpr::Parameters{{"q", query}}, cpr::Redirect{true},
                     cpr::Timeout{request_timeout},
                     cpr::Header{{"User-Agent", "CodeHarness/0.1"}});

        CH_LOG_DEBUG("WebSearchTool::fetch_search_page", "url={} status={} bytes={}",
                     response.url.str(), response.status_code, response.text.size());

        if (response.error) {
            return absl::UnavailableError(
                absl::StrCat("web_search failed: ", response.error.message));
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            return absl::InternalError(
                absl::StrCat("web_search failed: HTTP ", response.status_code));
        }

        return response.text;
    }

    [[nodiscard]] auto format_results(const std::string& query,
                                      const std::vector<SearchResult>& results) -> std::string {
        auto output = std::ostringstream{};
        output << "Search results for: " << query;
        for (std::size_t i = 0; i < results.size(); ++i) {
            output << '\n' << (i + 1) << ". " << results[i].title;
            output << '\n' << "   URL: " << results[i].url;
            if (!results[i].snippet.empty()) {
                output << '\n' << "   " << results[i].snippet;
            }
        }
        return output.str();
    }

}  // namespace

    auto WebSearchTool::name() const -> absl::string_view { return "web_search"; }

    auto WebSearchTool::description() const -> absl::string_view {
        return "Search the web and return compact top results with titles, URLs, and snippets.";
    }

    auto WebSearchTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"query", {{"type", "string"}, {"description", "Search query."}}},
                 {"max_results",
                  {{"type", "integer"},
                   {"default", default_max_results},
                   {"minimum", min_max_results},
                   {"maximum", max_max_results}}},
                 {"search_url",
                  {{"type", "string"},
                   {"description",
                    "Optional HTML search endpoint override, useful for private search backends "
                    "or testing."}}},
             }},
            {"required", {"query"}},
            {"additionalProperties", false},
        };
    }

    auto WebSearchTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto WebSearchTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto query = input.at("query").get<std::string>();
        const auto max_results = input.value("max_results", default_max_results);
        const auto endpoint = input.value("search_url", std::string{default_search_url});

        if (query.empty()) {
            return absl::InvalidArgumentError("query must not be empty");
        }
        if (max_results < min_max_results || max_results > max_max_results) {
            return absl::InvalidArgumentError("max_results must be between 1 and 10");
        }
        if (endpoint.empty() || !web::is_http_url(endpoint)) {
            return absl::InvalidArgumentError("search_url must start with http:// or https://");
        }

        const auto body = fetch_search_page(endpoint, query);
        if (!body.ok()) {
            return body.status();
        }

        const auto results = parse_search_results(*body, max_results);
        if (results.empty()) {
            return absl::NotFoundError("No search results found.");
        }

        return format_results(query, results);
    }

}  // namespace codeharness::tools

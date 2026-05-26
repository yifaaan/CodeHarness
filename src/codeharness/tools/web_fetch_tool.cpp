#include "codeharness/tools/web_fetch_tool.h"
#include "codeharness/tools/web_common.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/ascii.h>
#include <absl/strings/match.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <cpr/cpr.h>

#include <chrono>
#include <regex>
#include <string>
#include <utility>

#include "codeharness/logging.h"

namespace codeharness::tools {
namespace {

    constexpr int default_max_chars = 12000;
    constexpr int min_max_chars = 500;
    constexpr int max_max_chars = 50000;
    constexpr auto request_timeout = std::chrono::seconds{20};

    [[nodiscard]] auto html_to_text(std::string html) -> std::string {
        const auto script_or_style =
            std::regex{R"(<(script|style)\b[^>]*>[\s\S]*?</\1>)", std::regex_constants::icase};
        auto text = std::regex_replace(html, script_or_style, " ");
        text = std::regex_replace(text, std::regex{R"(<[^>]+>)"}, " ");
        text = web::decode_html_entities(std::move(text));
        text = std::regex_replace(text, std::regex{R"([ \t\r\f\v]+)"}, " ");
        absl::StripAsciiWhitespace(&text);
        return text;
    }

    [[nodiscard]] auto fetch_url(const std::string& url, int max_chars)
        -> absl::StatusOr<std::string> {
        const auto response =
            cpr::Get(cpr::Url{url}, cpr::Redirect{true}, cpr::Timeout{request_timeout},
                     cpr::Header{{"User-Agent", "CodeHarness/0.1"}});
        const auto effective_url = response.url.str();
        const auto content_type =
            response.header.contains("content-type") ? response.header.at("content-type")
                                                     : std::string{};

        CH_LOG_DEBUG("WebFetchTool::fetch_url", "url={} status={} bytes={}", effective_url,
                     response.status_code, response.text.size());

        if (response.error) {
            return absl::UnavailableError(
                absl::StrCat("web_fetch failed: ", response.error.message));
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            return absl::InternalError(
                absl::StrCat("web_fetch failed: HTTP ", response.status_code));
        }

        auto body = response.text;
        if (absl::StrContains(absl::AsciiStrToLower(content_type), "html")) {
            body = html_to_text(std::move(body));
        }
        absl::StripAsciiWhitespace(&body);

        if (body.size() > static_cast<std::size_t>(max_chars)) {
            body = body.substr(0, static_cast<std::size_t>(max_chars));
            absl::StripTrailingAsciiWhitespace(&body);
            body += "\n...[truncated]";
        }

        return absl::StrCat("URL: ", effective_url, "\n",
                            "Status: ", response.status_code, "\n",
                            "Content-Type: ",
                            content_type.empty() ? "(unknown)" : content_type, "\n\n", body);
    }

}  // namespace

    auto WebFetchTool::name() const -> absl::string_view { return "web_fetch"; }

    auto WebFetchTool::description() const -> absl::string_view {
        return "Fetch one web page and return compact readable text.";
    }

    auto WebFetchTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"url", {{"type", "string"}, {"description", "HTTP or HTTPS URL to fetch."}}},
                 {"max_chars",
                  {{"type", "integer"},
                   {"default", default_max_chars},
                   {"minimum", min_max_chars},
                   {"maximum", max_max_chars}}},
             }},
            {"required", {"url"}},
            {"additionalProperties", false},
        };
    }

    auto WebFetchTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto WebFetchTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto url = input.at("url").get<std::string>();
        const auto max_chars = input.value("max_chars", default_max_chars);
        if (url.empty()) {
            return absl::InvalidArgumentError("url must not be empty");
        }
        if (!web::is_http_url(url)) {
            return absl::InvalidArgumentError("url must start with http:// or https://");
        }
        if (max_chars < min_max_chars || max_chars > max_max_chars) {
            return absl::InvalidArgumentError("max_chars must be between 500 and 50000");
        }

        return fetch_url(url, max_chars);
    }

}  // namespace codeharness::tools

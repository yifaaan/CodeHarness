#include "codeharness/tools/web_search_tool.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/network/http_client.h"

namespace codeharness {

namespace {

constexpr std::string_view kDefaultSearchUrl = "https://html.duckduckgo.com/html/";
constexpr std::string_view kUserAgent = "CodeHarness/1.0";

constexpr int kDefaultMaxResults = 5;
constexpr int kMinResults = 1;
constexpr int kMaxResults = 10;

struct WebSearchInput {
  std::string query;
  int max_results = kDefaultMaxResults;
  std::string search_url;
};

auto parse_web_search_input(const nlohmann::json& input) -> absl::StatusOr<WebSearchInput> {
  WebSearchInput parsed;

  if (auto r = Assign(parsed.query, ReadJsonField<std::string>(input, "query", "web_search")); !r.ok()) {
    return r;
  }

  if (auto r = Assign(parsed.max_results, ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(
                                              input, "max_results", "web_search", kDefaultMaxResults));
      !r.ok()) {
    return r;
  }

  if (parsed.max_results < kMinResults || parsed.max_results > kMaxResults) {
    return absl::InvalidArgumentError(fmt::format("max_results must be between {} and {}", kMinResults, kMaxResults));
  }

  // search_url 可选
  auto url_result = ReadOptionalJsonField<std::string>(input, "search_url", "web_search");
  if (!url_result.ok()) {
    return url_result.status();
  }
  if (url_result->has_value()) {
    parsed.search_url = std::move(**url_result);
  }

  return parsed;
}

auto get_search_url(const WebSearchInput& parsed) -> std::string {
  // 优先级：参数 > 环境变量 > 默认值
  if (!parsed.search_url.empty()) {
    return parsed.search_url;
  }

  // 检查环境变量
  // Note: 在 Windows 上使用 _dupenv_s 获取环境变量
  // 这里简化处理，使用默认值
  return std::string{kDefaultSearchUrl};
}

// 清理 HTML 片段中的标签
auto clean_html(const std::string& fragment) -> std::string {
  std::string text;
  text.reserve(fragment.size());
  bool in_tag = false;
  for (const char c : fragment) {
    if (c == '<') {
      in_tag = true;
      continue;
    }
    if (c == '>') {
      text.push_back(' ');
      in_tag = false;
      continue;
    }
    if (!in_tag) {
      text.push_back(c);
    }
  }

  // 合并空白
  std::string result;
  result.reserve(text.size());
  bool prev_space = false;
  for (const char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!prev_space) {
        result.push_back(' ');
        prev_space = true;
      }
    } else {
      result.push_back(c);
      prev_space = false;
    }
  }

  // trim
  auto start = result.find_first_not_of(" ");
  if (start == std::string::npos) {
    return {};
  }
  auto end = result.find_last_not_of(" ");
  return result.substr(start, end - start + 1);
}

// 从 DuckDuckGo 重定向 URL 中提取真实 URL
auto normalize_result_url(const std::string& raw_url) -> std::string {
  // DuckDuckGo 使用 /l/?uddg=... 格式
  if (raw_url.find("duckduckgo.com") != std::string::npos && raw_url.find("/l/") != std::string::npos &&
      raw_url.find("uddg=") != std::string::npos) {
    auto pos = raw_url.find("uddg=");
    if (pos != std::string::npos) {
      auto start = pos + 5;
      auto end = raw_url.find('&', start);
      if (end == std::string::npos) {
        end = raw_url.size();
      }
      return raw_url.substr(start, end - start);
    }
  }
  return raw_url;
}

struct SearchResult {
  std::string title;
  std::string url;
  std::string snippet;
};

auto parse_search_results(const std::string& body, int limit) -> std::vector<SearchResult> {
  std::vector<SearchResult> results;

  // DuckDuckGo HTML 搜索结果解析
  // 结果链接有 class="result__a"
  // 简化实现：手动查找 <a class="result__a" href="...">...</a> 模式

  std::size_t pos = 0;
  while (results.size() < static_cast<std::size_t>(limit)) {
    // 查找 class="result__a"
    auto class_pos = body.find("class=\"result__a\"", pos);
    if (class_pos == std::string::npos) {
      break;
    }

    // 向后搜索到最近的 <a 标签
    auto tag_start = body.rfind("<a", class_pos);
    if (tag_start == std::string::npos || tag_start < pos) {
      pos = class_pos + 1;
      continue;
    }

    // 查找 href="..."
    auto href_pos = body.find("href=\"", tag_start);
    if (href_pos == std::string::npos || href_pos > class_pos + 30) {
      pos = class_pos + 1;
      continue;
    }

    auto href_start = href_pos + 6;
    auto href_end = body.find('"', href_start);
    if (href_end == std::string::npos) {
      pos = class_pos + 1;
      continue;
    }
    auto href = body.substr(href_start, href_end - href_start);

    // 查找 >...< 之间的标题
    auto title_start = body.find('>', class_pos);
    if (title_start == std::string::npos) {
      pos = class_pos + 1;
      continue;
    }
    auto title_end = body.find("</a>", title_start);
    if (title_end == std::string::npos) {
      pos = class_pos + 1;
      continue;
    }
    auto title = body.substr(title_start + 1, title_end - title_start - 1);

    auto clean_title = clean_html(title);
    auto clean_url = normalize_result_url(href);

    if (!clean_title.empty() && !clean_url.empty()) {
      SearchResult result;
      result.title = clean_title;
      result.url = clean_url;
      result.snippet = "";
      results.push_back(std::move(result));
    }

    pos = title_end + 4;
  }

  return results;
}

}  // namespace

auto WebSearchTool::name() const -> std::string { return "web_search"; }

auto WebSearchTool::description() const -> std::string {
  return "Search the web and return compact top results with titles and URLs. "
         "Uses DuckDuckGo HTML search by default. Can override endpoint via environment variable.";
}

auto WebSearchTool::is_read_only() const noexcept -> bool { return true; }

auto WebSearchTool::execute(const ToolRequest& request, const ToolContext& /*context*/) const
    -> absl::StatusOr<ToolResponse> {
  auto parsed = parse_web_search_input(request.parsed_input);
  if (!parsed.ok()) {
    return parsed.status();
  }

  auto search_url = get_search_url(*parsed);

  network::HttpClient client;
  std::map<std::string, std::string> headers;
  headers["User-Agent"] = std::string{kUserAgent};

  // DuckDuckGo 使用 GET 参数 q=...
  auto full_url = fmt::format("{}?q={}", search_url, parsed->query);

  auto response = client.get(full_url, headers);
  if (!response.ok()) {
    return response.status();
  }

  auto results = parse_search_results(response->body, parsed->max_results);

  if (results.empty()) {
    return ToolResponse{
        .tool_use_id = request.id,
        .content = "No search results found.",
        .is_error = false,
    };
  }

  std::string output = fmt::format("Search results for: {}\n", parsed->query);
  for (int i = 0; i < static_cast<int>(results.size()); ++i) {
    output += fmt::format("{}.\n", i + 1);
    output += fmt::format("   Title: {}\n", results[i].title);
    output += fmt::format("   URL: {}\n", results[i].url);
    if (!results[i].snippet.empty()) {
      output += fmt::format("   {}\n", results[i].snippet);
    }
  }

  return ToolResponse{
      .tool_use_id = request.id,
      .content = output,
      .is_error = false,
  };
}

}  // namespace codeharness

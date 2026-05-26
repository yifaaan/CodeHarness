#pragma once

#include <absl/strings/str_replace.h>
#include <absl/strings/string_view.h>
#include <ada.h>

#include <string>
#include <string_view>

namespace codeharness::tools::web {

    [[nodiscard]] inline auto is_http_url(absl::string_view url) -> bool {
        const auto parsed = ada::parse(std::string_view{url.data(), url.size()});
        if (!parsed) {
            return false;
        }

        const auto protocol = parsed->get_protocol();
        return protocol == "http:" || protocol == "https:";
    }

    [[nodiscard]] inline auto decode_html_entities(std::string text) -> std::string {
        return absl::StrReplaceAll(text,
                                   {
                                       {"&nbsp;", " "},
                                       {"&amp;", "&"},
                                       {"&lt;", "<"},
                                       {"&gt;", ">"},
                                       {"&quot;", "\""},
                                       {"&#39;", "'"},
                                   });
    }

}  // namespace codeharness::tools::web

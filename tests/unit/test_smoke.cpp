#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <absl/strings/str_cat.h>
#include <doctest/doctest.h>
#include <fmt/core.h>

#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/engine/message.h"

TEST_CASE("json dependency is available") {
    const nlohmann::json value{{"name", "CodeHarness"}, {"cpp", 23}};

    CHECK(value.at("name").get<std::string>() == "CodeHarness");
    CHECK(value.at("cpp").get<int>() == 23);
}

TEST_CASE("fmt dependency is available") {
    CHECK(fmt::format("{}{}", "agent-", "harness") == "agent-harness");
}

TEST_CASE("abseil dependency is available") {
    CHECK(absl::StrCat("code", "-", "harness") == "code-harness");
}

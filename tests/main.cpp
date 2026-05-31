#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "codeharness/core/message.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/version.h"

#include <vector>

TEST_CASE("project metadata is available")
{
    CHECK(codeharness::PROJECT_NAME == "CodeHarness");
    CHECK(codeharness::VERSION == "0.1.0");
}

TEST_CASE("echo provider returns latest user text")
{
    codeharness::EchoProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    auto result = provider.generate(std::span<const codeharness::Message>(messages));

    REQUIRE(result.has_value());
    CHECK(result->role == codeharness::Role::Assistant);
    CHECK(codeharness::collect_text(*result) == "hello");
}
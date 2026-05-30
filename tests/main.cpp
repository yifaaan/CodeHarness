#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "codeharness/version.h"

TEST_CASE("project metadata is available") {
    CHECK(codeharness::kProjectName == "CodeHarness");
    CHECK(codeharness::kVersion == "0.1.0");
}

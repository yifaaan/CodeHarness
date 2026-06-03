#include "test_support.h"

TEST_CASE("strict optional json field validates present values")
{
    const auto input = nlohmann::json{
        {"name", "deploy-pack"},
        {"enabled", "yes"},
    };

    auto missing = codeharness::read_optional_json_field<std::string>(input, "description", "plugin manifest");
    REQUIRE(missing.has_value());
    CHECK(!missing->has_value());

    auto name = codeharness::read_optional_json_field<std::string>(input, "name", "plugin manifest");
    REQUIRE(name.has_value());
    REQUIRE(name->has_value());
    CHECK(**name == "deploy-pack");

    auto invalid = codeharness::read_optional_json_field<bool>(input, "enabled", "plugin manifest");
    REQUIRE(!invalid.has_value());
    CHECK(invalid.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(invalid.error().message == "plugin manifest requires boolean field: enabled");
}

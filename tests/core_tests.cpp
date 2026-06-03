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

TEST_CASE("nullable json fields treat missing and null as fallback")
{
    const auto input = nlohmann::json{
        {"name", nullptr},
        {"tags", nlohmann::json::array({"one", "two"})},
    };

    auto missing = codeharness::read_nullable_json_field<std::vector<std::string>>(input, "missing", "task record");
    REQUIRE(missing.has_value());
    CHECK(missing->empty());

    auto null_name = codeharness::read_nullable_optional_json_field<std::string>(input, "name", "task record");
    REQUIRE(null_name.has_value());
    CHECK(!null_name->has_value());

    auto tags = codeharness::read_nullable_json_field<std::vector<std::string>>(input, "tags", "task record");
    REQUIRE(tags.has_value());
    CHECK(*tags == std::vector<std::string>{"one", "two"});
}

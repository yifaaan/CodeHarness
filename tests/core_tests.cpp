#include "codeharness/core/git.h"
#include "codeharness/core/strings.h"

#include "test_support.h"

TEST_CASE("trim removes leading and trailing whitespace")
{
    CHECK(codeharness::trim("") == "");
    CHECK(codeharness::trim("  ") == "");
    CHECK(codeharness::trim("\t\n\r ") == "");
    CHECK(codeharness::trim("hello") == "hello");
    CHECK(codeharness::trim("  hello  ") == "hello");
    CHECK(codeharness::trim("\n\ttext\n") == "text");
}

TEST_CASE("slugify normalizes text to filename-safe identifier")
{
    CHECK(codeharness::slugify("Hello World") == "hello_world");
    CHECK(codeharness::slugify("  Spaces  And  Stuff  ") == "spaces_and_stuff");
    CHECK(codeharness::slugify("special!@#chars") == "special_chars");
    CHECK(codeharness::slugify("---punctuation-") == "punctuation");
    CHECK(codeharness::slugify("") == "memory");
    CHECK(codeharness::slugify("___") == "memory");
    CHECK(codeharness::slugify("CamelCase123") == "camelcase123");
}

TEST_CASE("strip_trailing_cr removes a single trailing carriage return")
{
    CHECK(codeharness::strip_trailing_cr("hello\r") == "hello");
    CHECK(codeharness::strip_trailing_cr("hello\n") == "hello\n");
    CHECK(codeharness::strip_trailing_cr("hello") == "hello");
    CHECK(codeharness::strip_trailing_cr("") == "");
}

TEST_CASE("next_line extracts a line and returns the rest offset")
{
    auto [line1, off1] = codeharness::next_line("abc\ndef\nghi", 0);
    CHECK(line1 == "abc");
    CHECK(off1 == 4);

    auto [line2, off2] = codeharness::next_line("abc\ndef\nghi", off1);
    CHECK(line2 == "def");
    CHECK(off2 == 8);

    auto [line3, off3] = codeharness::next_line("abc\ndef\nghi", off2);
    CHECK(line3 == "ghi");
    CHECK(off3 == 11);

    // no newline at end — rest of text
    auto [rest, end] = codeharness::next_line("abc\ndef", 4);
    CHECK(rest == "def");
    CHECK(end == 7);
}

TEST_CASE("lower_ascii folds uppercase to lowercase")
{
    CHECK(codeharness::lower_ascii('A') == 'a');
    CHECK(codeharness::lower_ascii('Z') == 'z');
    CHECK(codeharness::lower_ascii('a') == 'a');
    CHECK(codeharness::lower_ascii('0') == '0');
    CHECK(codeharness::lower_ascii(' ') == ' ');
}

TEST_CASE("git_blob_hash_hex produces a 40-char SHA-1 hex string")
{
    auto hash = codeharness::git_blob_hash_hex("hello world");
    REQUIRE(hash.has_value());
    CHECK(hash->size() == 40);
    // SHA-1 of "blob 11\0hello world" per git object hashing
    CHECK(*hash == "95d09f2b10159347eece71399a7e2e907ea3df4f");
}

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

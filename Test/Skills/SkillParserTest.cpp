#include <doctest/doctest.h>

#include "Skills/SkillParser.h"

using namespace codeharness::skills;

TEST_SUITE("SkillParser")
{

	TEST_CASE("Parse empty content")
	{
		auto result = SkillParser::Parse("", "/path/to/skill.md", SkillSource::Extra);
		REQUIRE(result.ok());
		CHECK_EQ(result->name, "skill");
		CHECK(result->content.empty());
	}

	TEST_CASE("Parse content without frontmatter")
	{
		std::string content = "# My Skill\n\nThis is the skill content.";
		auto result = SkillParser::Parse(content, "/path/to/my-skill.md", SkillSource::Project);
		REQUIRE(result.ok());
		CHECK_EQ(result->name, "my-skill");
		CHECK_EQ(result->content, "# My Skill\n\nThis is the skill content.");
		CHECK_EQ(result->source, SkillSource::Project);
	}

	TEST_CASE("Parse with YAML frontmatter")
	{
		std::string content = "---\n"
							  "name: code-style\n"
							  "description: Applies code style conventions\n"
							  "type: prompt\n"
							  "whenToUse: When writing code\n"
							  "---\n"
							  "# Code Style Guide\n\n"
							  "Use camelCase for variables.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::User);
		REQUIRE(result.ok());

		CHECK_EQ(result->name, "code-style");
		CHECK_EQ(result->description, "Applies code style conventions");
		CHECK_EQ(result->metadata.type, SkillType::Prompt);
		CHECK(result->metadata.whenToUse.has_value());
		CHECK_EQ(*result->metadata.whenToUse, "When writing code");
		CHECK_EQ(result->content, "# Code Style Guide\n\nUse camelCase for variables.");
		CHECK_EQ(result->source, SkillSource::User);
	}

	TEST_CASE("Parse with arguments array")
	{
		std::string content = "---\n"
							  "name: review\n"
							  "arguments:\n"
							  "  - target\n"
							  "  - mode\n"
							  "---\n"
							  "Review $target in $mode mode.";

		auto result = SkillParser::Parse(content, "/path/to/review.md", SkillSource::Extra);
		REQUIRE(result.ok());

		CHECK_EQ(result->name, "review");
		CHECK_EQ(result->metadata.arguments.size(), 2);
		CHECK_EQ(result->metadata.arguments[0], "target");
		CHECK_EQ(result->metadata.arguments[1], "mode");
		CHECK_EQ(result->content, "Review $target in $mode mode.");
	}

	TEST_CASE("Parse with disableModelInvocation")
	{
		std::string content = "---\n"
							  "name: user-only\n"
							  "disableModelInvocation: true\n"
							  "---\n"
							  "User only skill.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Builtin);
		REQUIRE(result.ok());
		CHECK_EQ(result->metadata.disableModelInvocation, true);
	}

	TEST_CASE("Parse with model override")
	{
		std::string content = "---\n"
							  "name: special-skill\n"
							  "model: gpt-4\n"
							  "---\n"
							  "Content.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Extra);
		REQUIRE(result.ok());
		CHECK(result->metadata.model.has_value());
		CHECK_EQ(*result->metadata.model, "gpt-4");
	}

	TEST_CASE("Parse with inline type")
	{
		std::string content = "---\n"
							  "name: quick-action\n"
							  "type: inline\n"
							  "---\n"
							  "Quick action content.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Extra);
		REQUIRE(result.ok());
		CHECK_EQ(result->metadata.type, SkillType::Inline);
	}

	TEST_CASE("Parse SKILL.md extracts directory name")
	{
		std::string content = "---\n"
							  "description: A skill in a directory\n"
							  "---\n"
							  "Content.";

		auto result = SkillParser::Parse(content, "/path/to/my-skill/SKILL.md", SkillSource::Project);
		REQUIRE(result.ok());
		CHECK_EQ(result->name, "my-skill");
		CHECK_EQ(result->dir, "/path/to/my-skill");
	}

	TEST_CASE("Parse invalid YAML returns error")
	{
		std::string content = "---\n"
							  "name: [invalid yaml\n"
							  "---\n"
							  "Content.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Extra);
		CHECK_FALSE(result.ok());
	}

	TEST_CASE("Parse with Windows line endings")
	{
		std::string content = "---\r\n"
							  "name: win-skill\r\n"
							  "---\r\n"
							  "Content.";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Extra);
		REQUIRE(result.ok());
		CHECK_EQ(result->name, "win-skill");
	}

	TEST_CASE("Parse with extra whitespace in frontmatter")
	{
		std::string content = "---\n"
							  "name: spaced-skill\n"
							  "description: Has extra newlines\n"
							  "\n"
							  "---\n"
							  "\n"
							  "# Content";

		auto result = SkillParser::Parse(content, "/path/to/skill.md", SkillSource::Extra);
		REQUIRE(result.ok());
		CHECK_EQ(result->name, "spaced-skill");
	}

}
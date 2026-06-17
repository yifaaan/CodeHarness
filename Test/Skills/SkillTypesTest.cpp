#include <doctest/doctest.h>

#include <iostream>
#include <optional>
#include <string>

#include "Skills/SkillTypes.h"

using namespace codeharness::skills;

TEST_SUITE("SkillTypes")
{

	TEST_CASE("SkillTypeToString")
	{
		CHECK_EQ(SkillTypeToString(SkillType::Prompt), "prompt");
		CHECK_EQ(SkillTypeToString(SkillType::Inline), "inline");
		CHECK_EQ(SkillTypeToString(SkillType::Flow), "flow");
	}

	TEST_CASE("ParseSkillType")
	{
		CHECK_EQ(ParseSkillType("prompt"), SkillType::Prompt);
		CHECK_EQ(ParseSkillType("inline"), SkillType::Inline);
		CHECK_EQ(ParseSkillType("flow"), SkillType::Flow);
		CHECK_EQ(ParseSkillType("unknown"), std::nullopt);
		CHECK_EQ(ParseSkillType(""), std::nullopt);
	}

	TEST_CASE("SkillSourceToString")
	{
		CHECK_EQ(SkillSourceToString(SkillSource::Project), "project");
		CHECK_EQ(SkillSourceToString(SkillSource::User), "user");
		CHECK_EQ(SkillSourceToString(SkillSource::Extra), "extra");
		CHECK_EQ(SkillSourceToString(SkillSource::Builtin), "builtin");
	}

	TEST_CASE("ParseSkillSource")
	{
		CHECK_EQ(ParseSkillSource("project"), SkillSource::Project);
		CHECK_EQ(ParseSkillSource("user"), SkillSource::User);
		CHECK_EQ(ParseSkillSource("extra"), SkillSource::Extra);
		CHECK_EQ(ParseSkillSource("builtin"), SkillSource::Builtin);
		CHECK_EQ(ParseSkillSource("unknown"), std::nullopt);
		CHECK_EQ(ParseSkillSource(""), std::nullopt);
	}

	TEST_CASE("SkillMetadata defaults")
	{
		SkillMetadata meta;
		CHECK_EQ(meta.type, SkillType::Prompt);
		CHECK_EQ(meta.disableModelInvocation, false);
		CHECK(meta.whenToUse == std::nullopt);
		CHECK(meta.model == std::nullopt);
		CHECK(meta.arguments.empty());
	}

	TEST_CASE("SkillDefinition defaults")
	{
		SkillDefinition def;
		CHECK_EQ(def.source, SkillSource::Extra);
		CHECK(def.name.empty());
		CHECK(def.description.empty());
		CHECK(def.path.empty());
		CHECK(def.dir.empty());
		CHECK(def.content.empty());
	}

	TEST_CASE("SkillActivationPayload defaults")
	{
		SkillActivationPayload payload;
		CHECK_EQ(payload.origin, SkillOrigin::UserSlash);
		CHECK_EQ(payload.depth, 0);
		CHECK(payload.name.empty());
		CHECK(payload.args.empty());
	}

	TEST_CASE("SkillRoot construction")
	{
		SkillRoot root;
		root.path = "/path/to/skills";
		root.source = SkillSource::User;
		CHECK_EQ(root.path, "/path/to/skills");
		CHECK_EQ(root.source, SkillSource::User);
	}
}
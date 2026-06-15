#include <doctest/doctest.h>

#include "Skills/SkillRegistry.h"
#include "Skills/SkillParser.h"

using namespace codeharness::skills;

TEST_SUITE("SkillRegistry")
{

	TEST_CASE("Register and GetSkill")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "test-skill";
		def.description = "A test skill";
		def.content = "Test content";
		def.source = SkillSource::Extra;

		registry.Register(def);

		auto* found = registry.GetSkill("test-skill");
		REQUIRE(found != nullptr);
		CHECK_EQ(found->name, "test-skill");
		CHECK_EQ(found->description, "A test skill");
		CHECK_EQ(found->content, "Test content");
	}

	TEST_CASE("GetSkill returns nullptr for unknown skill")
	{
		SkillRegistry registry;
		auto* found = registry.GetSkill("unknown-skill");
		CHECK_EQ(found, nullptr);
	}

	TEST_CASE("Register respects priority (first wins)")
	{
		SkillRegistry registry;

		SkillDefinition def1;
		def1.name = "skill-a";
		def1.description = "First version";
		def1.source = SkillSource::Project;

		SkillDefinition def2;
		def2.name = "skill-a";
		def2.description = "Second version";
		def2.source = SkillSource::User;

		registry.Register(def1);
		registry.Register(def2);

		auto* found = registry.GetSkill("skill-a");
		REQUIRE(found != nullptr);
		CHECK_EQ(found->description, "First version");
		CHECK_EQ(found->source, SkillSource::Project);
	}

	TEST_CASE("ListSkills")
	{
		SkillRegistry registry;

		SkillDefinition def1;
		def1.name = "skill-1";
		registry.Register(def1);

		SkillDefinition def2;
		def2.name = "skill-2";
		registry.Register(def2);

		auto list = registry.ListSkills();
		CHECK_EQ(list.size(), 2);
	}

	TEST_CASE("ListInvocableSkills filters disabled skills")
	{
		SkillRegistry registry;

		SkillDefinition def1;
		def1.name = "invocable";
		def1.metadata.disableModelInvocation = false;
		registry.Register(def1);

		SkillDefinition def2;
		def2.name = "disabled";
		def2.metadata.disableModelInvocation = true;
		registry.Register(def2);

		auto list = registry.ListInvocableSkills();
		CHECK_EQ(list.size(), 1);
		CHECK_EQ(list[0]->name, "invocable");
	}

	TEST_CASE("Clear")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		registry.Register(def);

		registry.Clear();
		CHECK(registry.Empty());
		CHECK_EQ(registry.Size(), 0);
	}

	TEST_CASE("Size and Empty")
	{
		SkillRegistry registry;
		CHECK(registry.Empty());
		CHECK_EQ(registry.Size(), 0);

		SkillDefinition def;
		def.name = "skill";
		registry.Register(def);

		CHECK_FALSE(registry.Empty());
		CHECK_EQ(registry.Size(), 1);
	}

	TEST_CASE("RenderSkillPrompt expands $ARGUMENTS")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.content = "Arguments: $ARGUMENTS";
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "arg1 arg2", "session-123");
		CHECK_EQ(rendered, "Arguments: arg1 arg2");
	}

	TEST_CASE("RenderSkillPrompt expands positional args")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.content = "First: $0, Second: $1";
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "alpha beta", "session-123");
		CHECK_EQ(rendered, "First: alpha, Second: beta");
	}

	TEST_CASE("RenderSkillPrompt expands named args")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.content = "Target: $target, Mode: $mode";
		def.metadata.arguments = {"target", "mode"};
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "src/main.ts review", "session-123");
		CHECK_EQ(rendered, "Target: src/main.ts, Mode: review");
	}

	TEST_CASE("RenderSkillPrompt expands context variables")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.dir = "/path/to/skills/skill";
		def.content = "Dir: ${KIMI_SKILL_DIR}, Session: ${KIMI_SESSION_ID}";
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "", "session-abc");
		CHECK_EQ(rendered, "Dir: /path/to/skills/skill, Session: session-abc");
	}

	TEST_CASE("RenderSkillPrompt handles missing positional args")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.content = "First: $0, Second: $1, Third: $2";
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "only-one", "session-123");
		CHECK_EQ(rendered, "First: only-one, Second: $1, Third: $2");
	}

	TEST_CASE("RenderSkillPrompt handles quoted args")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "skill";
		def.content = "Path: $0, Options: $1";
		registry.Register(def);

		auto* found = registry.GetSkill("skill");
		REQUIRE(found != nullptr);

		auto rendered = registry.RenderSkillPrompt(*found, "\"path with spaces\" normal", "session-123");
		CHECK_EQ(rendered, "Path: path with spaces, Options: normal");
	}

	TEST_CASE("Register ignores empty name")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "";
		def.content = "Some content";

		registry.Register(def);
		CHECK(registry.Empty());
	}

	TEST_CASE("RenderSkillIndex is empty when no skills are registered")
	{
		SkillRegistry registry;
		CHECK_EQ(registry.RenderSkillIndex(), "");
	}

	TEST_CASE("RenderSkillIndex is empty when all skills are non-invocable")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "user-only";
		def.metadata.disableModelInvocation = true;
		registry.Register(def);

		CHECK_EQ(registry.RenderSkillIndex(), "");
	}

	TEST_CASE("RenderSkillIndex lists invocable skills with description and whenToUse")
	{
		SkillRegistry registry;

		SkillDefinition a;
		a.name = "review";
		a.description = "Review code changes";
		a.metadata.whenToUse = "When the user asks for a code review";
		registry.Register(a);

		SkillDefinition b;
		b.name = "hidden";
		b.metadata.disableModelInvocation = true; // filtered out
		registry.Register(b);

		auto rendered = registry.RenderSkillIndex();
		CHECK_FALSE(rendered.empty());
		CHECK(rendered.find("review") != std::string::npos);
		CHECK(rendered.find("Review code changes") != std::string::npos);
		CHECK(rendered.find("When the user asks for a code review") != std::string::npos);
		// The disabled skill must not appear.
		CHECK(rendered.find("hidden") == std::string::npos);
	}

	TEST_CASE("RenderSkillIndex includes the skill type")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "inline-skill";
		def.metadata.type = SkillType::Inline;
		registry.Register(def);

		auto rendered = registry.RenderSkillIndex();
		CHECK_FALSE(rendered.empty());
		CHECK(rendered.find("inline") != std::string::npos);
	}

}
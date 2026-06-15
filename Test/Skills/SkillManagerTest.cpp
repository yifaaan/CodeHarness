#include <doctest/doctest.h>

#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"

using namespace codeharness::skills;

TEST_SUITE("SkillManager")
{

	TEST_CASE("Activate returns error when registry is null")
	{
		SkillManager manager(nullptr);

		SkillActivationPayload payload;
		payload.name = "test-skill";

		auto status = manager.Activate(payload);
		CHECK_FALSE(status.ok());
	}

	TEST_CASE("Activate returns error for unknown skill")
	{
		SkillRegistry registry;
		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "unknown-skill";

		auto status = manager.Activate(payload);
		CHECK_FALSE(status.ok());
	}

	TEST_CASE("Activate returns error when depth exceeds max")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "test-skill";
		def.content = "Content";
		registry.Register(def);

		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "test-skill";
		payload.depth = SkillManager::MAX_DEPTH + 1;

		auto status = manager.Activate(payload);
		CHECK_FALSE(status.ok());
	}

	TEST_CASE("Activate succeeds for registered skill")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "test-skill";
		def.content = "Test content";
		registry.Register(def);

		SkillManager manager(&registry);
		manager.SetSessionId("session-123");

		SkillActivationPayload payload;
		payload.name = "test-skill";
		payload.args = "arg1 arg2";
		payload.origin = SkillOrigin::UserSlash;
		payload.depth = 0;

		auto status = manager.Activate(payload);
		CHECK(status.ok());
	}

	TEST_CASE("Activate with ModelTool origin")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "model-skill";
		def.content = "Content for model";
		registry.Register(def);

		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "model-skill";
		payload.origin = SkillOrigin::ModelTool;

		auto status = manager.Activate(payload);
		CHECK(status.ok());
	}

	TEST_CASE("Activate with NestedSkill origin")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "nested-skill";
		def.content = "Nested content";
		registry.Register(def);

		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "nested-skill";
		payload.origin = SkillOrigin::NestedSkill;
		payload.depth = 1;

		auto status = manager.Activate(payload);
		CHECK(status.ok());
	}

	TEST_CASE("SetSessionId")
	{
		SkillRegistry registry;
		SkillManager manager(&registry);

		manager.SetSessionId("new-session-id");
	}

	TEST_CASE("MAX_DEPTH constant")
	{
		CHECK_EQ(SkillManager::MAX_DEPTH, 3);
	}

	TEST_CASE("Activate expands variables in content")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "var-skill";
		def.content = "Args: $ARGUMENTS, First: $0";
		registry.Register(def);

		SkillManager manager(&registry);
		manager.SetSessionId("session-xyz");

		SkillActivationPayload payload;
		payload.name = "var-skill";
		payload.args = "hello world";

		auto status = manager.Activate(payload);
		CHECK(status.ok());
	}

	TEST_CASE("Activate at max depth succeeds")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "max-depth-skill";
		def.content = "Content";
		registry.Register(def);

		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "max-depth-skill";
		payload.depth = SkillManager::MAX_DEPTH;

		auto status = manager.Activate(payload);
		CHECK(status.ok());
	}

	TEST_CASE("Activate at max depth + 1 fails")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "over-depth-skill";
		def.content = "Content";
		registry.Register(def);

		SkillManager manager(&registry);

		SkillActivationPayload payload;
		payload.name = "over-depth-skill";
		payload.depth = SkillManager::MAX_DEPTH + 1;

		auto status = manager.Activate(payload);
		CHECK_FALSE(status.ok());
	}

}
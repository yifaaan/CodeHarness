#include <doctest/doctest.h>

#include "Engine/Tool.h"
#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"
#include "Skills/SkillTool.h"

using namespace codeharness::skills;
using namespace codeharness::tools;

TEST_SUITE("SkillTool")
{

	TEST_CASE("Name returns 'skill'")
	{
		SkillTool tool(nullptr);
		CHECK_EQ(tool.Name(), "skill");
	}

	TEST_CASE("Description is not empty")
	{
		SkillTool tool(nullptr);
		CHECK_FALSE(tool.Description().empty());
	}

	TEST_CASE("Parameters has required 'name' field")
	{
		SkillTool tool(nullptr);
		auto params = tool.Parameters();

		CHECK(params.is_object());
		CHECK(params.contains("properties"));
		CHECK(params["properties"].contains("name"));
		CHECK(params.contains("required"));

		auto required = params["required"];
		bool hasName = false;
		for (const auto& r : required)
		{
			if (r.get<std::string>() == "name")
				hasName = true;
		}
		CHECK(hasName);
	}

	TEST_CASE("ResolveExecution with valid args")
	{
		SkillTool tool(nullptr);

		nlohmann::json args;
		args["name"] = "test-skill";

		auto result = tool.ResolveExecution(args);
		REQUIRE(result.ok());
		CHECK_FALSE(result->requiresPermission);
		CHECK_FALSE(result->description.empty());
	}

	TEST_CASE("ResolveExecution with missing name returns error")
	{
		SkillTool tool(nullptr);

		nlohmann::json args;
		args["args"] = "some args";

		auto result = tool.ResolveExecution(args);
		CHECK_FALSE(result.ok());
	}

	TEST_CASE("ResolveExecution with non-object args returns error")
	{
		SkillTool tool(nullptr);

		nlohmann::json args = "string";

		auto result = tool.ResolveExecution(args);
		CHECK_FALSE(result.ok());
	}

	TEST_CASE("Execute returns error when manager is null")
	{
		SkillTool tool(nullptr);

		nlohmann::json args;
		args["name"] = "test-skill";

		codeharness::engine::ToolContext ctx;

		auto result = tool.Execute(args, ctx);
		CHECK_FALSE(result.ok());
	}

	TEST_CASE("Execute with valid skill")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "test-skill";
		def.content = "Test content";
		registry.Register(def);

		SkillManager manager(&registry);
		SkillTool tool(&manager);

		nlohmann::json args;
		args["name"] = "test-skill";
		args["args"] = "arg1 arg2";

		codeharness::engine::ToolContext ctx;

		auto result = tool.Execute(args, ctx);
		REQUIRE(result.ok());
		CHECK_FALSE(result->isError);
		CHECK_FALSE(result->content.empty());
	}

	TEST_CASE("Execute with unknown skill returns error result")
	{
		SkillRegistry registry;
		SkillManager manager(&registry);
		SkillTool tool(&manager);

		nlohmann::json args;
		args["name"] = "unknown-skill";

		codeharness::engine::ToolContext ctx;

		auto result = tool.Execute(args, ctx);
		REQUIRE(result.ok());
		CHECK(result->isError);
		CHECK_FALSE(result->content.empty());
	}

	TEST_CASE("Execute without args field")
	{
		SkillRegistry registry;

		SkillDefinition def;
		def.name = "no-args-skill";
		def.content = "Content";
		registry.Register(def);

		SkillManager manager(&registry);
		SkillTool tool(&manager);

		nlohmann::json args;
		args["name"] = "no-args-skill";

		codeharness::engine::ToolContext ctx;

		auto result = tool.Execute(args, ctx);
		REQUIRE(result.ok());
		CHECK_FALSE(result->isError);
	}

	TEST_CASE("GetToolDefinition returns valid schema")
	{
		SkillTool tool(nullptr);
		auto def = tool.GetToolDefinition();

		CHECK_EQ(def.name, "skill");
		CHECK_FALSE(def.description.empty());
		CHECK(def.inputSchema.is_object());
	}
}

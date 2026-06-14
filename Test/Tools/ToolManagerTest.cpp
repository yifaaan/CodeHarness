#include "Tools/ToolManager.h"

#include <doctest/doctest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "absl/status/statusor.h"
#include "Engine/Tool.h"

namespace tools = codeharness::tools;
namespace engine = codeharness::engine;
using json = nlohmann::json;

namespace
{

	// A trivial tool used only for registration tests.
	class StubTool : public engine::ExecutableTool
	{
	  public:
		explicit StubTool(std::string Name) : _name(std::move(Name)) {}
		std::string Name() const override
		{
			return _name;
		}
		std::string Description() const override
		{
			return "stub";
		}
		json Parameters() const override
		{
			return json::object();
		}
		absl::StatusOr<engine::ToolExecution> ResolveExecution(const json &) override
		{
			return engine::ToolExecution{.description = "stub"};
		}
		absl::StatusOr<engine::ToolResult> Execute(const json &, const engine::ToolContext &) override
		{
			return engine::ToolResult{.content = "stub"};
		}

	  private:
		std::string _name;
	};

} // namespace

TEST_CASE("ToolManager: register and find by name")
{
	tools::ToolManager mgr;
	mgr.Register(std::make_unique<StubTool>("alpha"));
	mgr.Register(std::make_unique<StubTool>("beta"));

	REQUIRE(mgr.Find("alpha") != nullptr);
	CHECK(mgr.Find("alpha")->Name() == "alpha");
	REQUIRE(mgr.Find("beta") != nullptr);
	CHECK(mgr.Size() == 2);
}

TEST_CASE("ToolManager: find unknown returns null")
{
	tools::ToolManager mgr;
	mgr.Register(std::make_unique<StubTool>("alpha"));
	CHECK(mgr.Find("nope") == nullptr);
}

TEST_CASE("ToolManager: LoopTools yields all registered pointers")
{
	tools::ToolManager mgr;
	mgr.Register(std::make_unique<StubTool>("alpha"));
	mgr.Register(std::make_unique<StubTool>("beta"));

	auto loopTools = mgr.LoopTools();
	REQUIRE(loopTools.size() == 2);
	CHECK(loopTools[0]->Name() == "alpha");
	CHECK(loopTools[1]->Name() == "beta");
}

TEST_CASE("ToolManager: registering nullptr is ignored")
{
	tools::ToolManager mgr;
	mgr.Register(nullptr);
	CHECK(mgr.Size() == 0);
}

#include "Config/ProviderManager.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "Config/Config.h"
#include "Config/ConfigTypes.h"
#include "Llm/ChatProvider.h"
#include "Llm/HttpClient.h"
#include "Llm/Types.h"

namespace config = codeharness::config;
namespace llm = codeharness::llm;

namespace
{

	class MockHttpClient : public llm::HttpClient
	{
	public:
		llm::HttpRequest captured;
		int responseStatus = 200;
		std::vector<std::string> responseChunks;

		absl::StatusOr<llm::HttpResponse> Request(const llm::HttpRequest& req) override
		{
			captured = req;
			return llm::HttpResponse{responseStatus, {}, ""};
		}

		absl::StatusOr<llm::HttpResponse> StreamRequest(const llm::HttpRequest& req, const llm::StreamChunkCallback& onChunk, std::stop_token = {}) override
		{
			captured = req;
			for (const auto& chunk : responseChunks)
			{
				if (!onChunk(chunk))
					break;
			}
			return llm::HttpResponse{responseStatus, {}, ""};
		}
	};

	std::string Sse(const std::string& data)
	{
		return "data: " + data + "\n\n";
	}

	// Drive a minimal Generate so we can inspect the request the provider built.
	std::optional<std::string> AuthBearer(const llm::HttpRequest& req)
	{
		for (const auto& [k, v] : req.headers)
		{
			if (k == "Authorization")
				return v;
		}
		return std::nullopt;
	}

	config::KimiConfig MakeConfig()
	{
		config::KimiConfig cfg;
		cfg.defaultModel = "gpt-4o";

		config::ProviderConfig openai;
		openai.type = config::ProviderType::OpenAi;
		openai.apiKey = "sk-wired-key";
		openai.baseUrl = "https://proxy.example.com/v1";
		cfg.providers["my-openai"] = openai;

		config::ModelAlias m;
		m.provider = "my-openai";
		m.model = "gpt-4o";
		cfg.models["gpt-4o"] = m;
		return cfg;
	}

} // namespace

TEST_CASE("ProviderManager: resolves alias to OpenAiProvider with correct model")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	config::ProviderManager mgr(MakeConfig(), &mock);
	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	CHECK(resolved->provider->Name() == "openai");
	CHECK(resolved->provider->ModelName() == "gpt-4o");
	CHECK(resolved->providerName == "my-openai");
	CHECK(resolved->providerType == config::ProviderType::OpenAi);
}

TEST_CASE("ProviderManager: wires api_key into Authorization header")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	config::ProviderManager mgr(MakeConfig(), &mock);
	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());

	CHECK(resolved->provider->Generate("", {}, {}, {}).ok());
	CHECK(AuthBearer(mock.captured).value_or("") == "Bearer sk-wired-key");
}

TEST_CASE("ProviderManager: splits base_url into host and path")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	config::ProviderManager mgr(MakeConfig(), &mock);
	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());

	CHECK(resolved->provider->Generate("", {}, {}, {}).ok());
	CHECK(mock.captured.host == "proxy.example.com");
	CHECK(mock.captured.path == "/v1/chat/completions");
}

TEST_CASE("ProviderManager: defaults host and path when base_url absent")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	auto cfg = MakeConfig();
	cfg.providers["my-openai"].baseUrl.reset();
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	CHECK(resolved->provider->Generate("", {}, {}, {}).ok());
	CHECK(mock.captured.host == "api.openai.com");
	CHECK(mock.captured.path == "/v1/chat/completions");
}

TEST_CASE("ProviderManager: uses env sub-table when api_key absent")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	auto cfg = MakeConfig();
	cfg.providers["my-openai"].apiKey.reset();
	cfg.providers["my-openai"].env["OPENAI_API_KEY"] = "sk-from-env-table";
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	CHECK(resolved->provider->Generate("", {}, {}, {}).ok());
	CHECK(AuthBearer(mock.captured).value_or("") == "Bearer sk-from-env-table");
}

TEST_CASE("ProviderManager: provider thinking overrides global block")
{
	MockHttpClient mock;
	auto cfg = MakeConfig();
	cfg.thinking = config::ThinkingConfig{.effort = llm::ThinkingEffort::Low};
	cfg.providers["my-openai"].thinking = config::ThinkingConfig{.effort = llm::ThinkingEffort::High};
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	REQUIRE(resolved->provider->ThinkingEffortLevel().has_value());
	CHECK(*resolved->provider->ThinkingEffortLevel() == llm::ThinkingEffort::High);
}

TEST_CASE("ProviderManager: global thinking applies when provider has none")
{
	MockHttpClient mock;
	auto cfg = MakeConfig();
	cfg.thinking = config::ThinkingConfig{.effort = llm::ThinkingEffort::Medium};
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	REQUIRE(resolved->provider->ThinkingEffortLevel().has_value());
	CHECK(*resolved->provider->ThinkingEffortLevel() == llm::ThinkingEffort::Medium);
}

TEST_CASE("ProviderManager: ResolveConfigForModel reports capability fields")
{
	MockHttpClient mock;
	config::ProviderManager mgr(MakeConfig(), &mock);

	auto rc = mgr.ResolveConfigForModel("gpt-4o");
	REQUIRE(rc.ok());
	CHECK(rc->modelName == "gpt-4o");
	CHECK(rc->providerType == config::ProviderType::OpenAi);
	CHECK(rc->supportsImages == true);
	CHECK(rc->maxTokens == 128000);
}

TEST_CASE("ProviderManager: unknown alias returns NotFound")
{
	MockHttpClient mock;
	config::ProviderManager mgr(MakeConfig(), &mock);

	auto resolved = mgr.ResolveForModel("does-not-exist");
	CHECK_FALSE(resolved.ok());
	CHECK(resolved.status().code() == absl::StatusCode::kNotFound);
}

TEST_CASE("ProviderManager: dangling provider reference returns FailedPrecondition")
{
	MockHttpClient mock;
	auto cfg = MakeConfig();
	cfg.models["orphan"].provider = "no-such-provider";
	cfg.models["orphan"].model = "x";
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("orphan");
	CHECK_FALSE(resolved.ok());
	CHECK(resolved.status().code() == absl::StatusCode::kFailedPrecondition);
}

TEST_CASE("ProviderManager: non-OpenAI provider type returns Unimplemented")
{
	MockHttpClient mock;
	auto cfg = MakeConfig();
	cfg.providers["anth"].type = config::ProviderType::Anthropic;
	cfg.providers["anth"].apiKey = "sk-ant";
	cfg.models["claude"].provider = "anth";
	cfg.models["claude"].model = "claude-sonnet-4";
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("claude");
	CHECK_FALSE(resolved.ok());
	CHECK(resolved.status().code() == absl::StatusCode::kUnimplemented);
}

TEST_CASE("ProviderManager: kimi provider type is OpenAI-compatible")
{
	MockHttpClient mock;
	mock.responseChunks = {Sse(R"({"choices":[{"delta":{},"finish_reason":"stop"}]})"), "data: [DONE]\n\n"};

	auto cfg = MakeConfig();
	cfg.providers["my-openai"].type = config::ProviderType::Kimi;
	config::ProviderManager mgr(cfg, &mock);

	auto resolved = mgr.ResolveForModel("gpt-4o");
	REQUIRE(resolved.ok());
	CHECK(resolved->providerType == config::ProviderType::Kimi);
	CHECK(resolved->provider->Name() == "openai");
}

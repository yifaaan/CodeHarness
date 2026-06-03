#include "test_support.h"

namespace
{

class FakeMcpTransport final : public codeharness::McpTransport
{
public:
    std::vector<nlohmann::json> sent_messages;
    bool started = false;
    bool closed = false;
    bool resources_not_found = false;
    std::map<std::string, nlohmann::json> tool_arguments;

    auto start() -> codeharness::Result<void> override
    {
        started = true;
        return {};
    }

    auto send(const nlohmann::json& message) -> codeharness::Result<void> override
    {
        sent_messages.push_back(message);

        if (message.contains("id"))
        {
            enqueue_response_for(message);
        }

        return {};
    }

    auto read() -> codeharness::Result<nlohmann::json> override
    {
        if (responses_.empty())
        {
            return codeharness::fail<nlohmann::json>(codeharness::ErrorKind::Network, "fake MCP transport has no queued response");
        }

        auto response = responses_.front();
        responses_.pop();
        return response;
    }

    auto close() noexcept -> void override
    {
        closed = true;
    }

private:
    auto enqueue_response_for(const nlohmann::json& request) -> void
    {
        const auto id = request.at("id").get<int>();
        const auto method = request.at("method").get<std::string>();

        if (method == "initialize")
        {
            responses_.push(
                nlohmann::json{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result",
                     nlohmann::json{
                         {"protocolVersion", "2024-11-05"},
                         {"capabilities", nlohmann::json::object()},
                         {"serverInfo", nlohmann::json{{"name", "fake"}, {"version", "1.0.0"}}},
                     }},
                });
            return;
        }

        if (method == "tools/list")
        {
            responses_.push(
                nlohmann::json{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result",
                     nlohmann::json{
                         {"tools",
                          nlohmann::json::array({nlohmann::json{
                              {"name", "echo.value"},
                              {"description", "Echo one value."},
                              {"inputSchema",
                               nlohmann::json{
                                   {"type", "object"},
                                   {"properties", nlohmann::json{{"value", nlohmann::json{{"type", "string"}}}}},
                                   {"required", nlohmann::json::array({"value"})},
                               }},
                          }})},
                     }},
                });
            return;
        }

        if (method == "resources/list")
        {
            if (resources_not_found)
            {
                responses_.push(
                    nlohmann::json{
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"error", nlohmann::json{{"code", -32601}, {"message", "Method not found"}}},
                    });
                return;
            }

            responses_.push(
                nlohmann::json{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result",
                     nlohmann::json{
                         {"resources",
                          nlohmann::json::array({nlohmann::json{
                              {"name", "Readme"},
                              {"uri", "file:///README.md"},
                              {"description", "Project readme."},
                          }})},
                     }},
                });
            return;
        }

        if (method == "tools/call")
        {
            const auto& params = request.at("params");
            const auto tool_name = params.at("name").get<std::string>();
            tool_arguments[tool_name] = params.at("arguments");
            responses_.push(
                nlohmann::json{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result",
                     nlohmann::json{
                         {"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "echo: " + tool_arguments[tool_name].dump()}}})},
                         {"isError", false},
                     }},
                });
            return;
        }

        responses_.push(
            nlohmann::json{
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", nlohmann::json{{"code", -32601}, {"message", "Method not found"}}},
            });
    }

    std::queue<nlohmann::json> responses_;
};

class FakeMcpExecutor final : public codeharness::McpToolExecutor
{
public:
    std::string seen_server;
    std::string seen_tool;
    nlohmann::json seen_arguments;
    codeharness::Result<codeharness::McpToolCallResult> next_result = codeharness::McpToolCallResult{.content = "ok"};

    auto call_tool(std::string_view server_name, std::string_view tool_name, const nlohmann::json& arguments) -> codeharness::Result<codeharness::McpToolCallResult> override
    {
        seen_server = std::string{server_name};
        seen_tool = std::string{tool_name};
        seen_arguments = arguments;
        return next_result;
    }
};

} // namespace

TEST_CASE("mcp json-rpc helpers build requests and parse responses")
{
    auto request = codeharness::make_mcp_request(7, "tools/list");

    CHECK(request.at("jsonrpc") == "2.0");
    CHECK(request.at("id") == 7);
    CHECK(request.at("method") == "tools/list");
    CHECK(request.at("params").is_object());

    auto response = codeharness::parse_mcp_response(
        nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", 7},
            {"result", nlohmann::json{{"tools", nlohmann::json::array()}}},
        });

    REQUIRE(response.has_value());
    CHECK(response->id == 7);
    REQUIRE(response->result.has_value());
    CHECK(response->result->at("tools").is_array());
}

TEST_CASE("mcp client initializes and lists server capabilities")
{
    auto transport = std::make_unique<FakeMcpTransport>();
    auto* observed_transport = transport.get();
    codeharness::McpClientSession session{std::move(transport)};

    auto initialized = session.initialize();
    REQUIRE(initialized.has_value());

    REQUIRE(observed_transport->sent_messages.size() == 2);
    CHECK(observed_transport->sent_messages[0].at("method") == "initialize");
    CHECK(observed_transport->sent_messages[1].at("method") == "notifications/initialized");
    CHECK(!observed_transport->sent_messages[1].contains("id"));

    auto tools = session.list_tools("fake-server");
    REQUIRE(tools.has_value());
    REQUIRE(tools->size() == 1);
    CHECK((*tools)[0].server_name == "fake-server");
    CHECK((*tools)[0].name == "echo.value");
    CHECK((*tools)[0].description == "Echo one value.");
    CHECK((*tools)[0].input_schema.at("required").at(0) == "value");

    auto resources = session.list_resources("fake-server");
    REQUIRE(resources.has_value());
    REQUIRE(resources->size() == 1);
    CHECK((*resources)[0].uri == "file:///README.md");
    CHECK((*resources)[0].name == "Readme");
}

TEST_CASE("mcp client treats missing resources list as optional")
{
    auto transport = std::make_unique<FakeMcpTransport>();
    transport->resources_not_found = true;
    codeharness::McpClientSession session{std::move(transport)};

    auto initialized = session.initialize();
    REQUIRE(initialized.has_value());

    auto resources = session.list_resources("minimal-server");
    REQUIRE(resources.has_value());
    CHECK(resources->empty());
}

TEST_CASE("mcp client calls tools and stringifies text content")
{
    auto transport = std::make_unique<FakeMcpTransport>();
    auto* observed_transport = transport.get();
    codeharness::McpClientSession session{std::move(transport)};

    auto initialized = session.initialize();
    REQUIRE(initialized.has_value());

    auto result = session.call_tool("echo.value", nlohmann::json{{"value", "hello"}});

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->content == R"(echo: {"value":"hello"})");
    CHECK(observed_transport->tool_arguments.at("echo.value").at("value") == "hello");
}

TEST_CASE("mcp stdio transport talks to a json-line server process")
{
    const auto server_path = std::string{CODEHARNESS_FAKE_MCP_SERVER};
    REQUIRE(!server_path.empty());
    REQUIRE(std::filesystem::exists(server_path));

    auto transport = std::make_unique<codeharness::McpStdioTransport>(
        codeharness::McpStdioServerConfig{
            .name = "fake-stdio",
            .command = server_path,
        },
        codeharness::McpStdioTransportOptions{
            .io_timeout_ms = 5000,
            .stop_timeout_ms = 1000,
        });
    codeharness::McpClientSession session{std::move(transport)};

    auto initialized = session.initialize();
    REQUIRE(initialized.has_value());

    auto tools = session.list_tools("fake-stdio");
    REQUIRE(tools.has_value());
    REQUIRE(tools->size() == 1);
    CHECK((*tools)[0].name == "echo");
    CHECK((*tools)[0].server_name == "fake-stdio");

    auto resources = session.list_resources("fake-stdio");
    REQUIRE(resources.has_value());
    CHECK(resources->empty());

    auto result = session.call_tool("echo", nlohmann::json{{"value", "hello over stdio"}});
    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->content == R"(stdio echo: {"value":"hello over stdio"})");
}

TEST_CASE("mcp stdio transport reports start and parse failures")
{
    codeharness::McpStdioTransport missing_server{
        codeharness::McpStdioServerConfig{
            .name = "missing-stdio",
            .command = "definitely-not-a-codeharness-test-binary",
        },
        codeharness::McpStdioTransportOptions{
            .io_timeout_ms = 1000,
            .stop_timeout_ms = 100,
        },
    };

    auto started = missing_server.start();
    REQUIRE(!started.has_value());
    CHECK(started.error().kind == codeharness::ErrorKind::Io);
    CHECK(started.error().message.find("failed to start MCP stdio server") != std::string::npos);

    const auto server_path = std::string{CODEHARNESS_FAKE_MCP_SERVER};
    codeharness::McpStdioTransport invalid_json_server{
        codeharness::McpStdioServerConfig{
            .name = "invalid-json-stdio",
            .command = server_path,
        },
        codeharness::McpStdioTransportOptions{
            .io_timeout_ms = 5000,
            .stop_timeout_ms = 1000,
        },
    };

    auto valid_start = invalid_json_server.start();
    REQUIRE(valid_start.has_value());

    auto sent = invalid_json_server.send("__invalid_json__");
    REQUIRE(sent.has_value());

    auto read = invalid_json_server.read();
    REQUIRE(!read.has_value());
    CHECK(read.error().kind == codeharness::ErrorKind::Network);
    CHECK(read.error().message.find("wrote invalid JSON line") != std::string::npos);
}

TEST_CASE("mcp tool adapter sanitizes names and forwards arguments")
{
    FakeMcpExecutor executor;
    executor.next_result = codeharness::McpToolCallResult{.content = "adapter output"};

    codeharness::McpToolAdapter adapter{
        executor,
        codeharness::McpToolInfo{
            .server_name = "git hub",
            .name = "create.issue",
            .description = "Create an issue.",
            .input_schema = nlohmann::json{{"type", "object"}},
        },
    };

    CHECK(adapter.name() == "mcp__git_hub__create_issue");
    CHECK(adapter.description() == "Create an issue.");
    CHECK(adapter.server_name() == "git hub");
    CHECK(adapter.tool_name() == "create.issue");

    codeharness::ToolRequest request;
    request.id = "mcp-use-1";
    request.name = adapter.name();
    set_request_input(request, R"({"title":"bug"})");

    codeharness::ToolContext context;
    auto response = adapter.execute(request, context);

    REQUIRE(response.has_value());
    CHECK(response->tool_use_id == "mcp-use-1");
    CHECK(response->content == "adapter output");
    CHECK(response->is_error == false);
    CHECK(executor.seen_server == "git hub");
    CHECK(executor.seen_tool == "create.issue");
    CHECK(executor.seen_arguments.at("title") == "bug");
}

TEST_CASE("mcp tool adapter returns mcp failures as tool errors")
{
    FakeMcpExecutor executor;
    executor.next_result = codeharness::fail<codeharness::McpToolCallResult>(codeharness::ErrorKind::Network, "server disconnected");

    codeharness::McpToolAdapter adapter{
        executor,
        codeharness::McpToolInfo{
            .server_name = "github",
            .name = "create_issue",
        },
    };

    codeharness::ToolRequest request;
    request.id = "mcp-use-failed";
    request.name = adapter.name();
    set_request_input(request, R"({"title":"bug"})");

    codeharness::ToolContext context;
    auto response = adapter.execute(request, context);

    REQUIRE(response.has_value());
    CHECK(response->tool_use_id == "mcp-use-failed");
    CHECK(response->is_error == true);
    CHECK(response->content.find("server disconnected") != std::string::npos);
}

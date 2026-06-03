#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace
{

auto write_response(int id, nlohmann::json result) -> void
{
    std::cout << nlohmann::json{
                     {"jsonrpc", "2.0"},
                     {"id", id},
                     {"result", std::move(result)},
                 }
              << '\n';
    std::cout.flush();
}

auto write_error(int id, int code, std::string message) -> void
{
    std::cout << nlohmann::json{
                     {"jsonrpc", "2.0"},
                     {"id", id},
                     {"error", nlohmann::json{{"code", code}, {"message", std::move(message)}}},
                 }
              << '\n';
    std::cout.flush();
}

auto handle_request(const nlohmann::json& request) -> void
{
    if (!request.contains("id"))
    {
        return;
    }

    const auto id = request.at("id").get<int>();
    const auto method = request.at("method").get<std::string>();

    if (method == "initialize")
    {
        write_response(
            id,
            nlohmann::json{
                {"protocolVersion", "2024-11-05"},
                {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}},
                {"serverInfo", nlohmann::json{{"name", "fake-mcp"}, {"version", "1.0.0"}}},
            });
        return;
    }

    if (method == "tools/list")
    {
        write_response(
            id,
            nlohmann::json{
                {"tools",
                 nlohmann::json::array({nlohmann::json{
                     {"name", "echo"},
                     {"description", "Echo the provided value."},
                     {"inputSchema",
                      nlohmann::json{
                          {"type", "object"},
                          {"properties", nlohmann::json{{"value", nlohmann::json{{"type", "string"}}}}},
                          {"required", nlohmann::json::array({"value"})},
                      }},
                 }})},
            });
        return;
    }

    if (method == "tools/call")
    {
        const auto& params = request.at("params");
        const auto arguments = params.at("arguments");
        write_response(
            id,
            nlohmann::json{
                {"content",
                 nlohmann::json::array({nlohmann::json{
                     {"type", "text"},
                     {"text", "stdio echo: " + arguments.dump()},
                 }})},
                {"isError", false},
            });
        return;
    }

    if (method == "resources/list")
    {
        write_error(id, -32601, "Method not found");
        return;
    }

    write_error(id, -32601, "Method not found");
}

} // namespace

auto main() -> int
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty())
        {
            continue;
        }

        try
        {
            const auto request = nlohmann::json::parse(line);
            if (request.is_string() && request.get<std::string>() == "__invalid_json__")
            {
                std::cout << "not json\n";
                std::cout.flush();
                continue;
            }

            handle_request(request);
        }
        catch (const std::exception& error)
        {
            std::cerr << "fake MCP server error: " << error.what() << '\n';
            return 1;
        }
    }

    return 0;
}

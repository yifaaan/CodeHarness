#include "codeharness/mcp/stdio_transport.h"

#include <spdlog/spdlog.h>
#include <nonstd/expected.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace codeharness
{

namespace
{

constexpr auto read_chunk_size = std::size_t{4096};
constexpr auto write_chunk_size = std::size_t{4096};

auto is_would_block(std::error_code error) noexcept -> bool
{
    return error == std::errc::resource_unavailable_try_again || error == std::errc::operation_would_block;
}

auto timeout_ms_since(std::chrono::steady_clock::time_point deadline) -> reproc::milliseconds
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
    {
        return reproc::milliseconds{0};
    }

    return std::chrono::duration_cast<reproc::milliseconds>(deadline - now);
}

auto trim_trailing_cr(std::string line) -> std::string
{
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    return line;
}

auto stop_actions(int timeout_ms) -> reproc::stop_actions
{
    const auto timeout = reproc::milliseconds{timeout_ms};
    return reproc::stop_actions{
        reproc::stop_action{reproc::stop::wait, timeout},
        reproc::stop_action{reproc::stop::terminate, timeout},
        reproc::stop_action{reproc::stop::kill, timeout},
    };
}

} // namespace

McpStdioTransport::McpStdioTransport(McpStdioServerConfig config, McpStdioTransportOptions options) : config_(std::move(config)), options_(options)
{
}

McpStdioTransport::~McpStdioTransport()
{
    close();
}

auto McpStdioTransport::start() -> Result<void>
{
    if (running_)
    {
        return {};
    }

    if (config_.command.empty())
    {
        return fail<void>(ErrorKind::Config, "MCP stdio server command is empty: " + config_.name);
    }

    std::vector<std::string> argv;
    argv.reserve(config_.args.size() + 1);
    argv.push_back(config_.command);
    argv.insert(argv.end(), config_.args.begin(), config_.args.end());

    auto cwd = std::string{};
    reproc::options options{};
    options.redirect.in.type = reproc::redirect::pipe;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;
    options.env.behavior = reproc::env::extend;
    options.env.extra = reproc::env{config_.env};
    // reproc's Windows backend rejects nonblocking mode for this three-pipe
    // stdio setup. We still avoid unbounded waits by polling before each read
    // and write and by writing bounded chunks.

    if (config_.cwd.has_value())
    {
        cwd = config_.cwd->string();
        options.working_directory = cwd.c_str();
    }

    if (auto error = process_.start(argv, options))
    {
        return fail<void>(ErrorKind::Io, "failed to start MCP stdio server " + config_.name + ": " + error.message());
    }

    running_ = true;
    return {};
}

auto McpStdioTransport::send(const nlohmann::json& message) -> Result<void>
{
    auto line = message.dump();
    line.push_back('\n');

    const auto* data = reinterpret_cast<const std::uint8_t*>(line.data());
    auto written = std::size_t{0};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{options_.io_timeout_ms};

    while (written < line.size())
    {
        const auto remaining_timeout = timeout_ms_since(deadline);
        if (remaining_timeout.count() <= 0)
        {
            return fail<void>(ErrorKind::Network, "timed out writing to MCP stdio server " + config_.name);
        }

        auto [events, poll_error] = process_.poll(reproc::event::in | reproc::event::exit, remaining_timeout);
        if (poll_error)
        {
            return fail<void>(ErrorKind::Network, "failed to poll MCP stdio server " + config_.name + ": " + poll_error.message());
        }

        if ((events & reproc::event::exit) != 0)
        {
            return fail<void>(ErrorKind::Network, "MCP stdio server exited while writing: " + config_.name);
        }

        if ((events & reproc::event::in) == 0)
        {
            continue;
        }

        const auto bytes_left = line.size() - written;
        const auto bytes_to_write = bytes_left > write_chunk_size ? write_chunk_size : bytes_left;
        auto [bytes_written, write_error] = process_.write(data + written, bytes_to_write);
        if (bytes_written > 0)
        {
            written += bytes_written;
        }

        if (!write_error)
        {
            continue;
        }

        if (is_would_block(write_error))
        {
            continue;
        }

        if (write_error == std::errc::broken_pipe)
        {
            return fail<void>(ErrorKind::Network, "MCP stdio server closed stdin: " + config_.name);
        }

        return fail<void>(ErrorKind::Network, "failed to write to MCP stdio server " + config_.name + ": " + write_error.message());
    }

    return {};
}

auto McpStdioTransport::read() -> Result<nlohmann::json>
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{options_.io_timeout_ms};
    while (true)
    {
        if (auto line = next_stdout_line())
        {
            if (line->empty())
            {
                continue;
            }

            try
            {
                return nlohmann::json::parse(*line);
            }
            catch (const nlohmann::json::parse_error& error)
            {
                return fail<nlohmann::json>(ErrorKind::Network, "MCP stdio server " + config_.name + " wrote invalid JSON line: " + error.what());
            }
        }

        const auto remaining_timeout = timeout_ms_since(deadline);
        if (remaining_timeout.count() == 0)
        {
            return fail<nlohmann::json>(ErrorKind::Network, "timed out waiting for MCP stdio server response: " + config_.name);
        }

        auto [events, poll_error] = process_.poll(reproc::event::out | reproc::event::err | reproc::event::exit, remaining_timeout);
        if (poll_error)
        {
            return fail<nlohmann::json>(ErrorKind::Network, "failed to poll MCP stdio server " + config_.name + ": " + poll_error.message());
        }

        if (events == 0)
        {
            continue;
        }

        if ((events & reproc::event::out) != 0)
        {
            if (auto read_out = read_available(reproc::stream::out, stdout_buffer_); !read_out)
            {
                return nonstd::make_unexpected(read_out.error());
            }
        }

        if ((events & reproc::event::err) != 0)
        {
            if (auto read_err = read_available(reproc::stream::err, stderr_buffer_); !read_err)
            {
                return nonstd::make_unexpected(read_err.error());
            }
            log_stderr_lines();
        }

        if ((events & reproc::event::exit) != 0)
        {
            (void)read_available(reproc::stream::out, stdout_buffer_);
            (void)read_available(reproc::stream::err, stderr_buffer_);
            log_stderr_lines();

            if (auto line = next_stdout_line())
            {
                try
                {
                    return nlohmann::json::parse(*line);
                }
                catch (const nlohmann::json::parse_error& error)
                {
                    return fail<nlohmann::json>(ErrorKind::Network, "MCP stdio server " + config_.name + " wrote invalid JSON line before exit: " + error.what());
                }
            }

            running_ = false;
            return fail<nlohmann::json>(ErrorKind::Network, "MCP stdio server exited before writing a response: " + config_.name);
        }
    }
}

auto McpStdioTransport::close() noexcept -> void
{
    if (!running_)
    {
        return;
    }

    if (auto error = process_.close(reproc::stream::in))
    {
        spdlog::debug("failed to close MCP stdio stdin for {}: {}", config_.name, error.message());
    }

    auto [status, error] = process_.stop(stop_actions(options_.stop_timeout_ms));
    (void)status;
    if (error)
    {
        spdlog::debug("failed to stop MCP stdio server {}: {}", config_.name, error.message());
    }

    running_ = false;
}

auto McpStdioTransport::read_available(reproc::stream stream, std::string& buffer) -> Result<void>
{
    std::array<std::uint8_t, read_chunk_size> chunk{};

    auto [bytes_read, read_error] = process_.read(stream, chunk.data(), chunk.size());
    if (read_error)
    {
        if (is_would_block(read_error) || read_error == std::errc::broken_pipe)
        {
            return {};
        }

        return fail<void>(ErrorKind::Network, "failed to read MCP stdio server " + config_.name + ": " + read_error.message());
    }

    if (bytes_read > 0)
    {
        buffer.append(reinterpret_cast<const char*>(chunk.data()), bytes_read);
    }

    return {};
}

auto McpStdioTransport::next_stdout_line() -> std::optional<std::string>
{
    const auto newline = stdout_buffer_.find('\n');
    if (newline == std::string::npos)
    {
        return std::nullopt;
    }

    auto line = stdout_buffer_.substr(0, newline);
    stdout_buffer_.erase(0, newline + 1);
    return trim_trailing_cr(std::move(line));
}

auto McpStdioTransport::log_stderr_lines() -> void
{
    while (true)
    {
        const auto newline = stderr_buffer_.find('\n');
        if (newline == std::string::npos)
        {
            return;
        }

        auto line = stderr_buffer_.substr(0, newline);
        stderr_buffer_.erase(0, newline + 1);
        line = trim_trailing_cr(std::move(line));
        if (!line.empty())
        {
            spdlog::debug("MCP stdio server {} stderr: {}", config_.name, line);
        }
    }
}

} // namespace codeharness

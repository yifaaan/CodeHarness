#pragma once

#include "codeharness/gateway/runtime_pool.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace codeharness::gateway
{

template <typename T>
class GatewayMessageQueue
{
public:
    GatewayMessageQueue() = default;
    ~GatewayMessageQueue() = default;

    GatewayMessageQueue(const GatewayMessageQueue&) = delete;
    auto operator=(const GatewayMessageQueue&) -> GatewayMessageQueue& = delete;

    GatewayMessageQueue(GatewayMessageQueue&& other) noexcept
    {
        std::scoped_lock lock{other.mutex_};
        messages_ = std::move(other.messages_);
    }

    auto operator=(GatewayMessageQueue&& other) noexcept -> GatewayMessageQueue&
    {
        if (this == &other)
        {
            return *this;
        }

        std::scoped_lock lock{mutex_, other.mutex_};
        messages_ = std::move(other.messages_);
        return *this;
    }

    auto push(T message) -> void
    {
        std::scoped_lock lock{mutex_};
        messages_.push(std::move(message));
    }

    auto try_pop() -> std::optional<T>
    {
        std::scoped_lock lock{mutex_};
        if (messages_.empty())
        {
            return std::nullopt;
        }

        auto message = std::move(messages_.front());
        messages_.pop();
        return message;
    }

    auto drain() -> std::vector<T>
    {
        std::scoped_lock lock{mutex_};

        std::vector<T> drained;
        drained.reserve(messages_.size());
        while (!messages_.empty())
        {
            drained.push_back(std::move(messages_.front()));
            messages_.pop();
        }

        return drained;
    }

    [[nodiscard]] auto size() const -> std::size_t
    {
        std::scoped_lock lock{mutex_};
        return messages_.size();
    }

    [[nodiscard]] auto empty() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return messages_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> messages_;
};

class GatewayMessageBus
{
public:
    GatewayMessageBus() = default;
    ~GatewayMessageBus();

    GatewayMessageBus(const GatewayMessageBus&) = delete;
    auto operator=(const GatewayMessageBus&) -> GatewayMessageBus& = delete;
    GatewayMessageBus(GatewayMessageBus&&) noexcept;
    auto operator=(GatewayMessageBus&&) noexcept -> GatewayMessageBus&;

    [[nodiscard]] auto inbound() noexcept -> GatewayMessageQueue<GatewayInboundMessage>&;
    [[nodiscard]] auto inbound() const noexcept -> const GatewayMessageQueue<GatewayInboundMessage>&;
    [[nodiscard]] auto outbound() noexcept -> GatewayMessageQueue<GatewayOutboundMessage>&;
    [[nodiscard]] auto outbound() const noexcept -> const GatewayMessageQueue<GatewayOutboundMessage>&;

private:
    GatewayMessageQueue<GatewayInboundMessage> inbound_;
    GatewayMessageQueue<GatewayOutboundMessage> outbound_;
};

} // namespace codeharness::gateway

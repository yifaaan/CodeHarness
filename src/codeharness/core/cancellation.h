#pragma once

#include <atomic>
#include <memory>

namespace codeharness
{

class CancellationToken
{
public:
    CancellationToken() = default;

    [[nodiscard]] auto is_cancelled() const noexcept -> bool
    {
        return state_ != nullptr && state_->load(std::memory_order_acquire);
    }

private:
    friend class CancellationSource;

    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> state) : state_{std::move(state)}
    {
    }

    std::shared_ptr<std::atomic<bool>> state_;
};

class CancellationSource
{
public:
    CancellationSource() : state_{std::make_shared<std::atomic<bool>>(false)}
    {
    }

    [[nodiscard]] auto token() const noexcept -> CancellationToken
    {
        return CancellationToken{state_};
    }

    auto cancel() const noexcept -> void
    {
        state_->store(true, std::memory_order_release);
    }

    [[nodiscard]] auto is_cancelled() const noexcept -> bool
    {
        return state_->load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<std::atomic<bool>> state_;
};

} // namespace codeharness

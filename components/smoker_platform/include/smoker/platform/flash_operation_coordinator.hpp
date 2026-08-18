#pragma once

#include <atomic>
#include <cstdint>

namespace smoker::platform {

enum class FlashOperationOwner : std::uint8_t { None, History, Ota };

class FlashOperationCoordinator final {
public:
    [[nodiscard]] bool try_acquire_history() noexcept
    {
        if (history_deferred_.load(std::memory_order_acquire)) return false;
        auto expected = FlashOperationOwner::None;
        return owner_.compare_exchange_strong(
            expected, FlashOperationOwner::History,
            std::memory_order_acq_rel, std::memory_order_relaxed
        );
    }

    void release_history() noexcept
    {
        auto expected = FlashOperationOwner::History;
        static_cast<void>(owner_.compare_exchange_strong(
            expected, FlashOperationOwner::None,
            std::memory_order_release, std::memory_order_relaxed
        ));
    }

    // OtaTask sets deferral before waiting for an in-progress bounded history
    // operation. No ControlTask code calls this coordinator.
    [[nodiscard]] bool try_acquire_ota() noexcept
    {
        history_deferred_.store(true, std::memory_order_release);
        auto expected = FlashOperationOwner::None;
        return owner_.compare_exchange_strong(
            expected, FlashOperationOwner::Ota,
            std::memory_order_acq_rel, std::memory_order_relaxed
        );
    }

    void release_ota(const bool keep_history_deferred = false) noexcept
    {
        auto expected = FlashOperationOwner::Ota;
        static_cast<void>(owner_.compare_exchange_strong(
            expected, FlashOperationOwner::None,
            std::memory_order_release, std::memory_order_relaxed
        ));
        if (!keep_history_deferred) {
            history_deferred_.store(false, std::memory_order_release);
        }
    }

    void set_history_deferred(const bool deferred) noexcept
    {
        history_deferred_.store(deferred, std::memory_order_release);
    }

    [[nodiscard]] bool history_deferred() const noexcept
    {
        return history_deferred_.load(std::memory_order_acquire);
    }

private:
    std::atomic<FlashOperationOwner> owner_{FlashOperationOwner::None};
    std::atomic_bool history_deferred_{false};
};

class HistoryFlashLease final {
public:
    explicit HistoryFlashLease(FlashOperationCoordinator& coordinator) noexcept
        : coordinator_{coordinator}
        , acquired_{coordinator_.try_acquire_history()}
    {
    }
    ~HistoryFlashLease() { if (acquired_) coordinator_.release_history(); }
    HistoryFlashLease(const HistoryFlashLease&) = delete;
    HistoryFlashLease& operator=(const HistoryFlashLease&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return acquired_; }

private:
    FlashOperationCoordinator& coordinator_;
    bool acquired_{false};
};

} // namespace smoker::platform

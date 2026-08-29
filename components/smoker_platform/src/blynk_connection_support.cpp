#include "smoker/platform/blynk_connection_support.hpp"

namespace smoker::platform {

std::uint32_t BlynkConnectionBoundary::callback_connected() noexcept
{
    auto generation = connection_generation_.fetch_add(
        1U, std::memory_order_relaxed
    ) + 1U;
    if (generation == 0U) {
        generation = connection_generation_.fetch_add(
            1U, std::memory_order_relaxed
        ) + 1U;
    }
    connected_.store(true, std::memory_order_release);
    return generation;
}

void BlynkConnectionBoundary::callback_disconnected() noexcept
{
    connected_.store(false, std::memory_order_release);
    disconnect_generation_.fetch_add(1U, std::memory_order_release);
}

std::uint32_t BlynkConnectionBoundary::callback_connection_generation() const noexcept
{
    return connection_generation_.load(std::memory_order_acquire);
}

BlynkConnectionSnapshot BlynkConnectionBoundary::poll() noexcept
{
    BlynkConnectionSnapshot result{};
    while (true) {
        const auto disconnect_before = disconnect_generation_.load(
            std::memory_order_acquire
        );
        result.connected = connected_.load(std::memory_order_acquire);
        result.connection_generation = connection_generation_.load(
            std::memory_order_acquire
        );
        const auto disconnect_after = disconnect_generation_.load(
            std::memory_order_acquire
        );
        if (disconnect_before == disconnect_after) {
            result.disconnect_generation = disconnect_after;
            break;
        }
    }

    result.cleanup_required = result.disconnect_generation
        != observed_disconnect_generation_;
    result.connection_started = result.connected
        && result.connection_generation != observed_connection_generation_;
    observed_disconnect_generation_ = result.disconnect_generation;
    // A callback publishes its new generation before publishing connected=true.
    // Do not acknowledge a half-observed connection until a later poll sees it
    // connected, otherwise its connect transition could be lost.
    if (result.connected) {
        observed_connection_generation_ = result.connection_generation;
    }
    return result;
}

bool BlynkConnectionBoundary::usable(
    const BlynkConnectionSnapshot& snapshot
) const noexcept
{
    if (!snapshot.connected) return false;
    const auto disconnect = disconnect_generation_.load(std::memory_order_acquire);
    const auto connection = connection_generation_.load(std::memory_order_acquire);
    const bool connected = connected_.load(std::memory_order_acquire);
    return connected
        && disconnect == snapshot.disconnect_generation
        && connection == snapshot.connection_generation;
}

bool BlynkConnectionBoundary::accepts(
    const std::uint32_t connection_generation
) const noexcept
{
    if (connection_generation == 0U) return false;
    const bool connected = connected_.load(std::memory_order_acquire);
    const auto current = connection_generation_.load(std::memory_order_acquire);
    return connected && current == connection_generation;
}

} // namespace smoker::platform

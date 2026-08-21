#pragma once

#include "smoker/app/commands.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace smoker::app {

enum class MailboxAdmission {
    Accepted,
    Full,
};

// One non-critical producer, one ControlTask consumer. Network transports use
// distinct instances. The consumer deliberately leaves moved-from slot storage
// intact so destruction/reuse happens on the non-critical producer side.
class SpscCommandMailbox final {
public:
    static constexpr std::size_t capacity = 16U;
    static constexpr std::size_t regular_admission_capacity = capacity - 1U;

    SpscCommandMailbox() = default;
    SpscCommandMailbox(const SpscCommandMailbox&) = delete;
    SpscCommandMailbox& operator=(const SpscCommandMailbox&) = delete;

    [[nodiscard]] MailboxAdmission push(
        Command command,
        std::uint32_t correlation_id = 0U,
        std::uint32_t transport_generation = 0U
    );
    [[nodiscard]] bool try_pop(
        Command& command,
        std::uint32_t* correlation_id = nullptr,
        std::uint32_t* transport_generation = nullptr
    ) noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;
    [[nodiscard]] std::size_t overflow_count() const noexcept;

private:
    struct TransportCommand final {
        Command command;
        std::uint32_t correlation_id{0U};
        std::uint32_t transport_generation{0U};
    };

    std::array<std::optional<TransportCommand>, capacity> commands_{};
    // ESP32-S3 implements 64-bit atomics through a global FreeRTOS critical
    // section. Native 32-bit monotonically wrapping sequences preserve the
    // bounded SPSC arithmetic without adding that lock to ControlTask.
    alignas(64) std::atomic<std::uint32_t> read_sequence_{0U};
    alignas(64) std::atomic<std::uint32_t> write_sequence_{0U};
    std::atomic<std::size_t> overflow_count_{0U};
};

} // namespace smoker::app

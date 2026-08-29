#pragma once

#include "smoker/app/command_mailbox.hpp"
#include "smoker/core/domain.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace smoker::platform {

inline constexpr std::uint32_t internal_ota_correlation_id = 0xFFFFFFFEU;

// Control readiness is an observable-cycle property, not an application
// health predicate. The sole ControlTask owns this latch and supplies only the
// two post-tick delivery/runtime results required for the one-shot transition.
class ControlReadinessLatch final {
public:
    [[nodiscard]] bool observe_cycle(
        bool snapshot_published,
        bool watchdog_reset_succeeded
    ) noexcept;
    [[nodiscard]] bool ready() const noexcept;

private:
    bool ready_{false};
};

// Shared by HTTP and Blynk producers. Both sequences deliberately skip zero
// and the internal OTA identity, including after uint32 wraparound.
class RuntimeIdGenerator final {
public:
    explicit RuntimeIdGenerator(
        std::uint32_t initial_session = 1U,
        std::uint32_t initial_correlation = 1U
    ) noexcept;

    [[nodiscard]] core::SessionId next_session() noexcept;
    [[nodiscard]] std::uint32_t next_correlation() noexcept;

private:
    [[nodiscard]] static std::uint32_t next_valid(
        std::atomic<std::uint32_t>& sequence
    ) noexcept;

    std::atomic<std::uint32_t> session_sequence_;
    std::atomic<std::uint32_t> correlation_sequence_;
};

struct CommandDrainResult final {
    std::size_t submitted{0U};
    std::size_t discarded{0U};
    bool stopped_at_barrier{false};
};

using ApplicationSubmitFunction = bool (*)(
    void* context,
    app::Command command,
    std::uint32_t correlation_id
) noexcept;

using BlynkGenerationValidator = bool (*)(
    const void* context,
    std::uint32_t connection_generation
) noexcept;

// The application queue has sixteen entries, one reserved for Stop. Each
// cycle drains at most thirteen external commands so both internal OTA intents
// can still be admitted. Sources alternate whenever both have work.
class RoundRobinCommandDrain final {
public:
    static constexpr std::size_t external_budget_per_cycle =
        app::SpscCommandMailbox::regular_admission_capacity - 2U;

    [[nodiscard]] CommandDrainResult drain(
        app::SpscCommandMailbox& http,
        app::SpscCommandMailbox& blynk,
        void* submit_context,
        ApplicationSubmitFunction submit,
        const void* blynk_generation_context = nullptr,
        BlynkGenerationValidator validate_blynk_generation = nullptr
    ) noexcept;

private:
    bool blynk_first_{false};
};

} // namespace smoker::platform

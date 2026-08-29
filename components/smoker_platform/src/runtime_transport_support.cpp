#include "smoker/platform/runtime_transport_support.hpp"

#include <utility>
#include <variant>

namespace smoker::platform {
namespace {

[[nodiscard]] bool reserved_id(const std::uint32_t value) noexcept
{
    return value == 0U || value == internal_ota_correlation_id;
}

} // namespace

bool ControlReadinessLatch::observe_cycle(
    const bool snapshot_published,
    const bool watchdog_reset_succeeded
) noexcept
{
    if (ready_ || !snapshot_published || !watchdog_reset_succeeded) {
        return false;
    }
    ready_ = true;
    return true;
}

bool ControlReadinessLatch::ready() const noexcept
{
    return ready_;
}

RuntimeIdGenerator::RuntimeIdGenerator(
    const std::uint32_t initial_session,
    const std::uint32_t initial_correlation
) noexcept
    : session_sequence_{initial_session}
    , correlation_sequence_{initial_correlation}
{
}

std::uint32_t RuntimeIdGenerator::next_valid(
    std::atomic<std::uint32_t>& sequence
) noexcept
{
    while (true) {
        const auto value = sequence.fetch_add(1U, std::memory_order_relaxed);
        if (!reserved_id(value)) {
            return value;
        }
    }
}

core::SessionId RuntimeIdGenerator::next_session() noexcept
{
    return next_valid(session_sequence_);
}

std::uint32_t RuntimeIdGenerator::next_correlation() noexcept
{
    return next_valid(correlation_sequence_);
}

CommandDrainResult RoundRobinCommandDrain::drain(
    app::SpscCommandMailbox& http,
    app::SpscCommandMailbox& blynk,
    void* const submit_context,
    const ApplicationSubmitFunction submit,
    const void* const blynk_generation_context,
    const BlynkGenerationValidator validate_blynk_generation
) noexcept
{
    CommandDrainResult result{};
    if (submit == nullptr) {
        return result;
    }

    app::Command command{app::StopSessionCommand{}};
    std::uint32_t correlation_id = 0U;
    std::uint32_t transport_generation = 0U;
    while (result.submitted + result.discarded < external_budget_per_cycle) {
        auto& preferred = blynk_first_ ? blynk : http;
        auto& alternate = blynk_first_ ? http : blynk;
        bool from_blynk = &preferred == &blynk;
        bool popped = preferred.try_pop(
            command, &correlation_id, &transport_generation
        );
        if (!popped) {
            from_blynk = &alternate == &blynk;
            popped = alternate.try_pop(
                command, &correlation_id, &transport_generation
            );
        }
        if (!popped) {
            break;
        }

        blynk_first_ = !blynk_first_;
        if (from_blynk && validate_blynk_generation != nullptr
            && !validate_blynk_generation(
                blynk_generation_context, transport_generation
            )) {
            ++result.discarded;
            continue;
        }
        const bool is_stop =
            std::holds_alternative<app::StopSessionCommand>(command);
        static_cast<void>(submit(
            submit_context, std::move(command), correlation_id
        ));
        ++result.submitted;
        if (is_stop) {
            result.stopped_at_barrier = true;
            break;
        }
    }
    return result;
}

} // namespace smoker::platform

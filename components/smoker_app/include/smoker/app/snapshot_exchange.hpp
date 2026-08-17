#pragma once

#include "smoker/app/snapshot_view.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace smoker::app {

class SnapshotExchange final {
public:
    class ReadLease final {
    public:
        ReadLease() noexcept = default;
        ReadLease(const ReadLease&) = delete;
        ReadLease& operator=(const ReadLease&) = delete;
        ReadLease(ReadLease&& other) noexcept;
        ReadLease& operator=(ReadLease&& other) noexcept;
        ~ReadLease();

        [[nodiscard]] SmokerSnapshotView view() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        friend class SnapshotExchange;
        ReadLease(const SnapshotExchange* exchange, std::size_t index) noexcept;
        void release() noexcept;

        const SnapshotExchange* exchange_{nullptr};
        std::size_t index_{0U};
    };

    SnapshotExchange(std::size_t probe_capacity, std::size_t alarm_capacity);
    SnapshotExchange(const SnapshotExchange&) = delete;
    SnapshotExchange& operator=(const SnapshotExchange&) = delete;

    // Never waits and never allocates. Returns false when capacities are
    // exceeded or both non-current buffers are leased.
    [[nodiscard]] bool publish(const SmokerSnapshotView& snapshot) noexcept;
    [[nodiscard]] ReadLease acquire() const noexcept;
    [[nodiscard]] std::size_t dropped_publish_count() const noexcept;

private:
    static constexpr std::size_t slot_count = 3U;
    static constexpr std::size_t no_slot = slot_count;

    struct Slot final {
        core::SessionStatus session_status{core::SessionStatus::Idle};
        std::optional<core::SessionId> session_id;
        core::StopReason stop_reason{core::StopReason::None};
        std::optional<core::Temperature> chamber_temperature;
        std::optional<core::Temperature> chamber_target;
        core::HeaterDemand heater_demand{core::HeaterDemand::off()};
        core::TimerRuntimeState timer;
        std::vector<ProbeSnapshotView> probes;
        std::size_t probe_count{0U};
        std::vector<core::Alarm> alarms;
        std::size_t alarm_count{0U};
        std::optional<core::Fault> active_fault;
        bool firmware_update_active{false};
        std::optional<core::Temperature> maximum_chamber_temperature;
        std::size_t command_queue_overflow_count{0U};
        std::array<CommandResultView, command_result_capacity> command_results{};
        std::size_t command_result_count{0U};
    };

    [[nodiscard]] SmokerSnapshotView view(std::size_t index) const noexcept;
    void release(std::size_t index) const noexcept;

    std::array<Slot, slot_count> slots_;
    mutable std::array<std::atomic<std::size_t>, slot_count> readers_{};
    std::atomic<std::size_t> published_index_{no_slot};
    std::atomic<std::size_t> dropped_publish_count_{0U};
};

} // namespace smoker::app

#pragma once

#include "smoker/core/domain.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace smoker::app {

inline constexpr std::size_t command_result_capacity = 16U;

struct CommandResultView final {
    std::uint32_t correlation_id{0U};
    bool semantic_accepted{false};
};

struct ProbeSnapshotView final {
    core::ProbeId id{};
    std::string_view name;
    core::ProbeRole role{core::ProbeRole::Unassigned};
    std::optional<core::Temperature> current_temperature;
    std::optional<core::Temperature> target_temperature;
    bool enabled{false};
    bool alarm_enabled{false};
};

// A synchronous, read-only view of application-owned state. Its spans remain
// valid until the next owning ControlTask tick. Cross-task consumers must copy
// it through SnapshotExchange rather than retaining this view directly.
struct SmokerSnapshotView final {
    core::SessionStatus session_status{core::SessionStatus::Idle};
    std::optional<core::SessionId> session_id;
    core::StopReason stop_reason{core::StopReason::None};
    std::optional<core::Temperature> chamber_temperature;
    std::optional<core::Temperature> chamber_target;
    core::HeaterDemand heater_demand{core::HeaterDemand::off()};
    core::TimerRuntimeState timer;
    std::span<const ProbeSnapshotView> probes;
    std::span<const core::Alarm> active_alarms;
    std::optional<core::Fault> active_fault;
    bool firmware_update_active{false};
    std::optional<core::Temperature> maximum_chamber_temperature;
    std::size_t command_queue_overflow_count{0U};
    std::span<const CommandResultView> command_results;
};

} // namespace smoker::app

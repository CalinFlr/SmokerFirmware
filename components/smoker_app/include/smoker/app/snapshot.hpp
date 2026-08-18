#pragma once

#include "smoker/core/domain.hpp"
#include "smoker/app/snapshot_view.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace smoker::app {

struct ProbeSnapshot final {
    core::ProbeId id{};
    std::string name;
    core::ProbeRole role{core::ProbeRole::Unassigned};
    std::optional<core::Temperature> current_temperature;
    std::optional<core::Temperature> target_temperature;
    bool enabled{false};
    bool alarm_enabled{false};
};

struct SmokerSnapshot final {
    core::SessionStatus session_status{core::SessionStatus::Idle};
    std::optional<core::SessionId> session_id;
    core::Duration session_elapsed{};
    std::optional<core::Recipe> recipe_snapshot;
    core::StopReason stop_reason{core::StopReason::None};
    std::optional<core::Temperature> chamber_temperature;
    std::optional<core::Temperature> chamber_target;
    core::HeaterDemand heater_demand{core::HeaterDemand::off()};
    core::TimerRuntimeState timer;
    std::vector<ProbeSnapshot> probes;
    std::vector<core::Alarm> active_alarms;
    std::optional<core::Fault> active_fault;
    bool firmware_update_active{false};
    std::size_t command_queue_overflow_count{0U};
    std::vector<CommandResultView> command_results;
};

} // namespace smoker::app

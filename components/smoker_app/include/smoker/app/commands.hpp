#pragma once

#include "smoker/core/domain.hpp"

#include <optional>
#include <variant>

namespace smoker::app {

struct StartSessionCommand final {
    core::SessionId session_id{};
    core::Recipe recipe;
};

struct StopSessionCommand final {
};

struct SetChamberTargetCommand final {
    std::optional<core::Temperature> target;
};

struct SetProbeTargetCommand final {
    core::ProbeId probe_id{};
    std::optional<core::Temperature> target;
};

struct SetProbeEnabledCommand final {
    core::ProbeId probe_id{};
    bool enabled{false};
};

struct SetProbeAlarmEnabledCommand final {
    core::ProbeId probe_id{};
    bool enabled{false};
};

struct AcknowledgeAlarmCommand final {
    core::AlarmId alarm_id{};
};

struct ClearResolvedFaultCommand final {
};

// Internal ControlTask-owned policy commands. External tasks must cross the
// bounded command transport and may never mutate update permission directly.
struct PrepareFirmwareUpdateCommand final {
};

struct FinishFirmwareUpdateCommand final {
};

using Command = std::variant<
    StartSessionCommand,
    StopSessionCommand,
    SetChamberTargetCommand,
    SetProbeTargetCommand,
    SetProbeEnabledCommand,
    SetProbeAlarmEnabledCommand,
    AcknowledgeAlarmCommand,
    ClearResolvedFaultCommand,
    PrepareFirmwareUpdateCommand,
    FinishFirmwareUpdateCommand>;

} // namespace smoker::app

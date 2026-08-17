#pragma once

#include "smoker/core/heater_demand.hpp"
#include "smoker/core/temperature.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace smoker::core {

using RecipeId = std::uint32_t;
using SessionId = std::uint32_t;
using StageId = std::uint32_t;
using ProbeId = std::uint8_t;
using AlarmId = std::uint32_t;
using Duration = std::chrono::milliseconds;

struct MonotonicClock final {
    using rep = Duration::rep;
    using period = Duration::period;
    using duration = Duration;
    using time_point = std::chrono::time_point<MonotonicClock, duration>;

    static constexpr bool is_steady = true;
};

using MonotonicTimePoint = MonotonicClock::time_point;

enum class ProbeRole {
    Meat,
    AmbientMonitor,
    Unassigned,
};

struct FoodProbeConfig final {
    ProbeId id{};
    std::string name;
    ProbeRole role{ProbeRole::Unassigned};
    std::optional<Temperature> target_temperature;
    bool enabled{true};
    bool alarm_enabled{true};
};

struct ProbeReading final {
    ProbeId id{};
    std::optional<Temperature> temperature;
};

enum class TimerStartConditionType {
    Immediate,
    ChamberTemperatureAtLeast,
    ProbeTemperatureAtLeast,
};

struct TimerStartCondition final {
    TimerStartConditionType type{TimerStartConditionType::Immediate};
    std::optional<Temperature> temperature;
    std::optional<ProbeId> probe_id;
};

enum class TimerCompletionAction {
    Notify,
    StopSession,
};

struct StageTimer final {
    Duration duration{};
    TimerStartCondition start_condition;
    TimerCompletionAction completion_action{TimerCompletionAction::Notify};
};

struct TimerRuntimeState final {
    bool started{false};
    bool completed{false};
    std::optional<MonotonicTimePoint> started_at;
    Duration elapsed{};
};

struct Stage final {
    StageId id{};
    std::string name;
    std::optional<Temperature> chamber_target;
    std::optional<StageTimer> timer;
};

struct Recipe final {
    RecipeId id{};
    std::string name;
    Stage stage;
};

enum class SessionStatus {
    Idle,
    Running,
    Stopped,
    Fault,
};

enum class StopReason {
    None,
    User,
    TimerCompleted,
    Fault,
    RecoveryNotAllowed,
};

struct Session final {
    SessionId id{};
    SessionStatus status{SessionStatus::Idle};
    Recipe recipe_snapshot;
    MonotonicTimePoint started_at{};
    std::optional<MonotonicTimePoint> stopped_at;
    std::optional<Temperature> active_chamber_target;
    TimerRuntimeState timer;
    StopReason stop_reason{StopReason::None};
};

enum class FaultCode {
    ChamberSensorInvalid,
    ChamberOverTemperature,
    ControlLoopFailure,
    ConfigurationInvalid,
};

struct Fault final {
    FaultCode code{FaultCode::ConfigurationInvalid};
    MonotonicTimePoint occurred_at{};
    bool latched{true};
};

enum class AlarmCode {
    ProbeTargetReached,
    ProbeDisconnected,
    TimerCompleted,
};

struct Alarm final {
    AlarmId id{};
    AlarmCode code{AlarmCode::TimerCompleted};
    std::optional<ProbeId> probe_id;
    MonotonicTimePoint occurred_at{};
    bool acknowledged{false};
    bool resolved{false};
};

enum class EventType {
    DeviceBooted,
    SessionStarted,
    SessionStopped,
    ChamberTargetChanged,
    ProbeTargetChanged,
    ProbeConnected,
    ProbeReconnected,
    ProbeDisconnected,
    ProbeTargetReached,
    TimerStarted,
    TimerCompleted,
    FaultRaised,
    FaultAcknowledged,
    AlarmAcknowledged,
    CommandQueueOverflow,
    CommandRejected,
};

struct Event final {
    EventType type{EventType::DeviceBooted};
    MonotonicTimePoint occurred_at{};
    std::optional<ProbeId> probe_id;
    std::optional<AlarmId> alarm_id;
    std::optional<FaultCode> fault_code;
};

struct SafetyLimits final {
    Temperature maximum_chamber_temperature;
};

} // namespace smoker::core

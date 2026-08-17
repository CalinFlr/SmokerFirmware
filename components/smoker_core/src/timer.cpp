#include "smoker/core/timer.hpp"

#include <algorithm>

namespace smoker::core {
namespace {

[[nodiscard]] bool threshold_reached(
    const TimerStartCondition& condition,
    const std::optional<Temperature>& chamber_temperature,
    const std::span<const ProbeReading> probe_readings
) noexcept
{
    switch (condition.type) {
    case TimerStartConditionType::Immediate:
        return true;
    case TimerStartConditionType::ChamberTemperatureAtLeast:
        return chamber_temperature && condition.temperature
            && *chamber_temperature >= *condition.temperature;
    case TimerStartConditionType::ProbeTemperatureAtLeast:
        if (!condition.temperature || !condition.probe_id) {
            return false;
        }

        for (const auto& reading : probe_readings) {
            if (reading.id == *condition.probe_id && reading.temperature
                && *reading.temperature >= *condition.temperature) {
                return true;
            }
        }
        return false;
    }

    return false;
}

} // namespace

bool is_valid_timer_configuration(const StageTimer& timer) noexcept
{
    if (timer.duration <= Duration::zero()) {
        return false;
    }

    const auto& condition = timer.start_condition;
    switch (condition.type) {
    case TimerStartConditionType::Immediate:
        return !condition.temperature && !condition.probe_id;
    case TimerStartConditionType::ChamberTemperatureAtLeast:
        return condition.temperature.has_value() && !condition.probe_id;
    case TimerStartConditionType::ProbeTemperatureAtLeast:
        return condition.temperature.has_value() && condition.probe_id.has_value();
    }

    return false;
}

TimerUpdate update_stage_timer(
    const StageTimer& configuration,
    TimerRuntimeState& state,
    const MonotonicTimePoint now,
    const std::optional<Temperature>& chamber_temperature,
    const std::span<const ProbeReading> probe_readings
) noexcept
{
    TimerUpdate update;
    if (state.completed || !is_valid_timer_configuration(configuration)) {
        return update;
    }

    if (!state.started) {
        if (!threshold_reached(configuration.start_condition, chamber_temperature, probe_readings)) {
            return update;
        }

        state.started = true;
        state.started_at = now;
        state.elapsed = Duration::zero();
        update.started_now = true;
    }

    if (!state.started_at || now < *state.started_at) {
        return update;
    }

    state.elapsed = std::min(now - *state.started_at, configuration.duration);
    if (state.elapsed >= configuration.duration) {
        state.completed = true;
        update.completed_now = true;
    }

    return update;
}

void freeze_stage_timer(TimerRuntimeState& state, const MonotonicTimePoint now) noexcept
{
    if (!state.started || state.completed || !state.started_at || now < *state.started_at) {
        return;
    }

    state.elapsed = now - *state.started_at;
}

} // namespace smoker::core

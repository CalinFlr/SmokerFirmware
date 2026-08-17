#pragma once

#include "smoker/core/domain.hpp"

#include <optional>
#include <span>

namespace smoker::core {

struct TimerUpdate final {
    bool started_now{false};
    bool completed_now{false};
};

[[nodiscard]] bool is_valid_timer_configuration(const StageTimer& timer) noexcept;

[[nodiscard]] TimerUpdate update_stage_timer(
    const StageTimer& configuration,
    TimerRuntimeState& state,
    MonotonicTimePoint now,
    const std::optional<Temperature>& chamber_temperature,
    std::span<const ProbeReading> probe_readings
) noexcept;

void freeze_stage_timer(TimerRuntimeState& state, MonotonicTimePoint now) noexcept;

} // namespace smoker::core

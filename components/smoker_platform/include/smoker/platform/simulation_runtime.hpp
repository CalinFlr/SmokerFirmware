#pragma once

#include "smoker/core/domain.hpp"

namespace smoker::platform {

struct SimulationRuntimeConfiguration final {
    core::Temperature ambient_temperature;
    core::FoodProbeConfig food_probe;
    core::SafetyLimits safety_limits;
    core::Recipe startup_recipe;
};

[[nodiscard]] bool start_simulation_runtime(SimulationRuntimeConfiguration configuration) noexcept;

} // namespace smoker::platform

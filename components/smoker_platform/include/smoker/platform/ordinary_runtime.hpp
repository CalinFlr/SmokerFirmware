#pragma once

#include "smoker/core/domain.hpp"

namespace smoker::platform {

struct OrdinaryRuntimeConfiguration final {
    core::Temperature simulated_food_temperature;
    core::FoodProbeConfig food_probe;
    core::SafetyLimits safety_limits;
    core::Recipe startup_recipe;
};

[[nodiscard]] bool start_ordinary_runtime(OrdinaryRuntimeConfiguration configuration) noexcept;

} // namespace smoker::platform

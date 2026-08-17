#pragma once

#include "smoker/core/heater_demand.hpp"
#include "smoker/core/temperature.hpp"

#include <optional>

namespace smoker::core {

[[nodiscard]] HeaterDemand calculate_heater_demand(
    const std::optional<Temperature>& chamber_temperature,
    const std::optional<Temperature>& chamber_target
) noexcept;

} // namespace smoker::core

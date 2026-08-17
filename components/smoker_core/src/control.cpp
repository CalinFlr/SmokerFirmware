#include "smoker/core/control.hpp"

namespace smoker::core {

HeaterDemand calculate_heater_demand(
    const std::optional<Temperature>& chamber_temperature,
    const std::optional<Temperature>& chamber_target
) noexcept
{
    if (!chamber_temperature || !chamber_target || *chamber_temperature >= *chamber_target) {
        return HeaterDemand::off();
    }

    const auto full_demand = HeaterDemand::from_percent(100.0F);
    return full_demand.value_or(HeaterDemand::off());
}

} // namespace smoker::core

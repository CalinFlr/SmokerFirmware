#include "smoker/core/heater_demand.hpp"

#include <cmath>

namespace smoker::core {

std::optional<HeaterDemand> HeaterDemand::from_percent(const float percent) noexcept
{
    constexpr float minimum_percent = 0.0F;
    constexpr float maximum_percent = 100.0F;

    if (!std::isfinite(percent) || percent < minimum_percent || percent > maximum_percent) {
        return std::nullopt;
    }

    return HeaterDemand{percent};
}

} // namespace smoker::core

#include "smoker/core/temperature.hpp"

#include <cmath>

namespace smoker::core {

std::optional<Temperature> Temperature::from_celsius(const float celsius) noexcept
{
    if (!std::isfinite(celsius)) {
        return std::nullopt;
    }

    return Temperature{celsius};
}

} // namespace smoker::core

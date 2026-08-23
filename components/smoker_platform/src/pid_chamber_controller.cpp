#include "smoker/platform/pid_chamber_controller.hpp"

#include <cmath>

namespace smoker::platform {

bool valid_pid_controller_configuration(
    const PidControllerConfiguration& configuration
) noexcept
{
    const bool common_values_are_valid =
        std::isfinite(configuration.proportional_gain)
        && std::isfinite(configuration.integral_gain)
        && std::isfinite(configuration.derivative_gain)
        && std::isfinite(configuration.minimum_output_percent)
        && std::isfinite(configuration.maximum_output_percent)
        && configuration.proportional_gain >= 0.0F
        && configuration.integral_gain >= 0.0F
        && configuration.derivative_gain >= 0.0F
        && configuration.minimum_output_percent >= 0.0F
        && configuration.maximum_output_percent <= 100.0F
        && configuration.minimum_output_percent
            <= configuration.maximum_output_percent;
    if (!common_values_are_valid) return false;

    switch (configuration.calculation_form) {
    case PidCalculationForm::Positional: {
        if (!configuration.positional_accumulated_error_bounds) return false;
        const auto bounds = *configuration.positional_accumulated_error_bounds;
        return std::isfinite(bounds.minimum)
            && std::isfinite(bounds.maximum)
            && bounds.minimum <= 0.0F
            && bounds.maximum >= 0.0F
            && bounds.minimum <= bounds.maximum;
    }
    case PidCalculationForm::Incremental:
        return !configuration.positional_accumulated_error_bounds;
    }

    return false;
}

PidChamberController::PidChamberController(
    const PidControllerConfiguration configuration,
    IPidControllerBackend& backend
) noexcept
    : configuration_{configuration}
    , backend_{backend}
{
    initialized_ = valid_pid_controller_configuration(configuration_)
        && backend_.initialize(configuration_);
}

std::optional<core::HeaterDemand> PidChamberController::request(
    const core::Temperature chamber_temperature,
    const core::Temperature chamber_target
) noexcept
{
    if (!initialized_) return std::nullopt;

    // The sign convention is fixed at this boundary: positive error means the
    // target is hotter than the authoritative measured chamber temperature.
    const float error = chamber_target.celsius() - chamber_temperature.celsius();
    if (!std::isfinite(error)) return std::nullopt;

    float output_percent = 0.0F;
    if (!backend_.compute(error, output_percent)
        || !std::isfinite(output_percent)
        || output_percent < configuration_.minimum_output_percent
        || output_percent > configuration_.maximum_output_percent) {
        return std::nullopt;
    }
    return core::HeaterDemand::from_percent(output_percent);
}

bool PidChamberController::reset() noexcept
{
    return initialized_ && backend_.reset();
}

bool PidChamberController::initialized() const noexcept
{
    return initialized_;
}

} // namespace smoker::platform

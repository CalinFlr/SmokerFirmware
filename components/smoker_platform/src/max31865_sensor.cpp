#include "smoker/platform/max31865_sensor.hpp"

#include <cmath>

namespace smoker::platform {

Max31865ReadinessPolicy::Max31865ReadinessPolicy(
    const Max31865FilterFrequency filter,
    const app::IClock& clock
) noexcept
    : clock_{clock}
    , first_conversion_time_{max31865_maximum_first_conversion_time(filter)}
{
}

void Max31865ReadinessPolicy::continuous_configuration_applied() noexcept
{
    ready_at_ = clock_.now() + first_conversion_time_;
}

void Max31865ReadinessPolicy::invalidate() noexcept
{
    ready_at_.reset();
}

bool Max31865ReadinessPolicy::sample_ready() const noexcept
{
    return ready_at_.has_value() && clock_.now() >= *ready_at_;
}

bool valid_max31865_conversion_configuration(
    const Max31865ConversionConfiguration& configuration
) noexcept
{
    if (!std::isfinite(configuration.reference_resistance_ohms)
        || configuration.reference_resistance_ohms <= 0.0F) {
        return false;
    }

    switch (configuration.filter) {
    case Max31865FilterFrequency::Hz50:
    case Max31865FilterFrequency::Hz60:
        break;
    default:
        return false;
    }

    switch (configuration.standard) {
    case Max31865RtdStandard::Its90:
    case Max31865RtdStandard::Din43760:
    case Max31865RtdStandard::UsIndustrial:
        return true;
    default:
        return false;
    }
}

bool valid_max31865_temperature_validity_policy(
    const Max31865TemperatureValidityPolicy& policy
) noexcept
{
    return std::isfinite(policy.minimum_celsius)
        && std::isfinite(policy.maximum_celsius)
        && policy.minimum_celsius < policy.maximum_celsius;
}

Max31865ChamberSensor::Max31865ChamberSensor(
    IMax31865Backend& backend,
    const Max31865TemperatureValidityPolicy validity_policy
) noexcept
    : backend_{backend}
    , validity_policy_{validity_policy}
{
    if (valid_max31865_temperature_validity_policy(validity_policy_)) {
        configured_ = backend_.initialize()
            == Max31865InitializationStatus::ConfiguredAwaitingFirstSample;
    }
}

std::optional<core::Temperature> Max31865ChamberSensor::read() noexcept
{
    if (!configured_) return std::nullopt;

    const auto result = backend_.read_continuous();
    if (result.status != Max31865ReadStatus::Valid
        || !std::isfinite(result.celsius)
        || result.celsius < validity_policy_.minimum_celsius
        || result.celsius > validity_policy_.maximum_celsius) {
        return std::nullopt;
    }
    return core::Temperature::from_celsius(result.celsius);
}

bool Max31865ChamberSensor::configured() const noexcept
{
    return configured_;
}

} // namespace smoker::platform

#pragma once

#include "smoker/app/ports.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace smoker::platform {

inline constexpr float max31865_pt100_nominal_ohms = 100.0F;

enum class Max31865FilterFrequency : std::uint8_t {
    Hz50,
    Hz60,
};

enum class Max31865RtdStandard : std::uint8_t {
    Its90,
    Din43760,
    UsIndustrial,
};

// The fitted reference resistor, filter, and RTD standard are required facts.
// This type deliberately supplies no defaults for still-unknown hardware data.
struct Max31865ConversionConfiguration final {
    float reference_resistance_ohms;
    Max31865FilterFrequency filter;
    Max31865RtdStandard standard;
};

// Sensor-specific operational validity. Bounds are mandatory, inclusive, and
// deliberately separate from the global Temperature domain.
struct Max31865TemperatureValidityPolicy final {
    float minimum_celsius;
    float maximum_celsius;
};

[[nodiscard]] bool valid_max31865_temperature_validity_policy(
    const Max31865TemperatureValidityPolicy& policy
) noexcept;

[[nodiscard]] bool valid_max31865_conversion_configuration(
    const Max31865ConversionConfiguration& configuration
) noexcept;

[[nodiscard]] constexpr core::Duration max31865_maximum_first_conversion_time(
    const Max31865FilterFrequency filter
) noexcept
{
    return filter == Max31865FilterFrequency::Hz60
        ? std::chrono::milliseconds{55}
        : std::chrono::milliseconds{66};
}

// Positive startup interval and tick rate. vTaskDelay() consumes the current
// partial tick first, so reserve one tick in addition to rounding up the full
// conversion interval. The caller checks that the result fits its tick type.
[[nodiscard]] constexpr std::uint64_t max31865_bootstrap_delay_ticks(
    const std::uint32_t boundary_milliseconds,
    const std::uint32_t ticks_per_second
) noexcept
{
    const auto scaled_interval =
        static_cast<std::uint64_t>(boundary_milliseconds) * ticks_per_second;
    return (scaled_interval + 999U) / 1'000U + 1U;
}

// Descriptor/configuration success does not make the RTD registers fresh.
// This allocation-free policy applies the official filter-dependent maximum
// first-conversion interval after each successful continuous configuration.
class Max31865ReadinessPolicy final {
public:
    Max31865ReadinessPolicy(
        Max31865FilterFrequency filter,
        const app::IClock& clock
    ) noexcept;

    void continuous_configuration_applied() noexcept;
    void invalidate() noexcept;
    [[nodiscard]] bool sample_ready() const noexcept;

private:
    const app::IClock& clock_;
    core::Duration first_conversion_time_;
    std::optional<core::MonotonicTimePoint> ready_at_;
};

enum class Max31865InitializationStatus : std::uint8_t {
    ConfiguredAwaitingFirstSample,
    InvalidConfiguration,
    DescriptorError,
    DeviceConfigurationError,
};

enum class Max31865ReadStatus : std::uint8_t {
    Valid,
    NotReady,
    DriverError,
    Fault,
};

struct Max31865ReadResult final {
    Max31865ReadStatus status{Max31865ReadStatus::DriverError};
    float celsius{0.0F};
};

// Platform-neutral seam for host tests. A target implementation owns the real
// driver descriptor and configures provisional continuous conversion once.
class IMax31865Backend {
public:
    virtual ~IMax31865Backend() = default;

    [[nodiscard]] virtual Max31865InitializationStatus initialize() noexcept = 0;
    [[nodiscard]] virtual Max31865ReadResult read_continuous() noexcept = 0;
};

class Max31865ChamberSensor final : public app::IChamberSensor {
public:
    Max31865ChamberSensor(
        IMax31865Backend& backend,
        Max31865TemperatureValidityPolicy validity_policy
    ) noexcept;

    [[nodiscard]] std::optional<core::Temperature> read() noexcept override;
    [[nodiscard]] bool configured() const noexcept;

private:
    IMax31865Backend& backend_;
    Max31865TemperatureValidityPolicy validity_policy_;
    bool configured_{false};
};

} // namespace smoker::platform

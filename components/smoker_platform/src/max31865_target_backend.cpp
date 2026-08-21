#include "smoker/platform/max31865_target_backend.hpp"

#include "esp_err.h"

namespace smoker::platform {
namespace {

[[nodiscard]] max31865_filter_t driver_filter(
    const Max31865FilterFrequency filter
) noexcept
{
    return filter == Max31865FilterFrequency::Hz50
        ? MAX31865_FILTER_50HZ
        : MAX31865_FILTER_60HZ;
}

[[nodiscard]] max31865_standard_t driver_standard(
    const Max31865RtdStandard standard
) noexcept
{
    switch (standard) {
    case Max31865RtdStandard::Its90:
        return MAX31865_ITS90;
    case Max31865RtdStandard::Din43760:
        return MAX31865_DIN43760;
    case Max31865RtdStandard::UsIndustrial:
        return MAX31865_US_INDUSTRIAL;
    }
    return MAX31865_ITS90;
}

} // namespace

Max31865TargetBackend::Max31865TargetBackend(
    const Max31865TargetConfiguration configuration,
    const app::IClock& clock
) noexcept
    : configuration_{configuration}
    , readiness_{configuration.conversion.filter, clock}
{
}

Max31865TargetBackend::~Max31865TargetBackend()
{
    release_descriptor();
}

Max31865InitializationStatus Max31865TargetBackend::initialize() noexcept
{
    release_descriptor();
    if (!valid_configuration()) {
        return Max31865InitializationStatus::InvalidConfiguration;
    }

    descriptor_.standard = driver_standard(configuration_.conversion.standard);
    descriptor_.r_ref = configuration_.conversion.reference_resistance_ohms;
    descriptor_.rtd_nominal = max31865_pt100_nominal_ohms;
    if (max31865_init_desc(
            &descriptor_,
            configuration_.spi_host,
            configuration_.clock_speed_hz,
            configuration_.chip_select_gpio
        ) != ESP_OK) {
        return Max31865InitializationStatus::DescriptorError;
    }
    descriptor_acquired_ = true;

    if (!configure_device()) {
        release_descriptor();
        return Max31865InitializationStatus::DeviceConfigurationError;
    }

    configured_ = true;
    return Max31865InitializationStatus::ConfiguredAwaitingFirstSample;
}

Max31865ReadResult Max31865TargetBackend::read_continuous() noexcept
{
    if (!configured_) return {Max31865ReadStatus::DriverError, 0.0F};
    if (!readiness_.sample_ready()) {
        return {Max31865ReadStatus::NotReady, 0.0F};
    }

    std::uint8_t fault_status = 0U;
    if (max31865_get_fault_status(&descriptor_, &fault_status) != ESP_OK) {
        return {Max31865ReadStatus::DriverError, 0.0F};
    }
    if (fault_status != 0U) {
        clear_fault_for_later_read();
        return {Max31865ReadStatus::Fault, 0.0F};
    }

    float celsius = 0.0F;
    if (max31865_read_temperature(&descriptor_, &celsius) == ESP_OK) {
        return {Max31865ReadStatus::Valid, celsius};
    }

    fault_status = 0U;
    if (max31865_get_fault_status(&descriptor_, &fault_status) == ESP_OK
        && fault_status != 0U) {
        clear_fault_for_later_read();
        return {Max31865ReadStatus::Fault, 0.0F};
    }
    return {Max31865ReadStatus::DriverError, 0.0F};
}

void Max31865TargetBackend::release_descriptor() noexcept
{
    configured_ = false;
    readiness_.invalidate();
    if (!descriptor_acquired_) return;
    static_cast<void>(max31865_free_desc(&descriptor_));
    descriptor_acquired_ = false;
}

bool Max31865TargetBackend::configure_device() noexcept
{
    readiness_.invalidate();
    max31865_config_t device_configuration{};
    device_configuration.mode = MAX31865_MODE_AUTO;
    device_configuration.connection = MAX31865_3WIRE;
    device_configuration.v_bias = true;
    device_configuration.filter = driver_filter(configuration_.conversion.filter);
    if (max31865_set_config(&descriptor_, &device_configuration) != ESP_OK) {
        return false;
    }
    readiness_.continuous_configuration_applied();
    return true;
}

void Max31865TargetBackend::clear_fault_for_later_read() noexcept
{
    // Clearing a latched MAX31865 fault changes its configuration register, so
    // the pinned driver requires the provisional continuous setup to be
    // written again. This read still reports the fault as absent; recovery can
    // only yield a fresh value on a later control cycle.
    readiness_.invalidate();
    if (max31865_clear_fault_status(&descriptor_) != ESP_OK
        || !configure_device()) {
        configured_ = false;
    }
}

bool Max31865TargetBackend::valid_configuration() const noexcept
{
    const bool valid_host = configuration_.spi_host == SPI2_HOST
        || configuration_.spi_host == SPI3_HOST;
    return valid_host
        && GPIO_IS_VALID_OUTPUT_GPIO(configuration_.chip_select_gpio)
        && configuration_.clock_speed_hz > 0U
        && configuration_.clock_speed_hz <= MAX31865_MAX_CLOCK_SPEED_HZ
        && valid_max31865_conversion_configuration(configuration_.conversion);
}

} // namespace smoker::platform

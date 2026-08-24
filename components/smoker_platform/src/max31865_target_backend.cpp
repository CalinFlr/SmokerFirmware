#include "smoker/platform/max31865_target_backend.hpp"

#include "smoker/platform/max31865_board_pins.hpp"

#include "esp_err.h"
#include "esp_log.h"

#include <cstdint>

namespace smoker::platform {
namespace {

constexpr char tag[] = "smoker_v0";
constexpr std::uint8_t configuration_register = 0x00U;
constexpr std::uint8_t write_register_mask = 0x80U;
constexpr std::uint8_t fault_clear_command_bit = 0x02U;

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
    const app::IClock& clock,
    const Max31865SpiBusOwner* const bus_owner
) noexcept
    : configuration_{configuration}
    , bus_owner_{bus_owner}
    , readiness_{configuration.conversion.filter, clock}
{
}

Max31865TargetBackend::~Max31865TargetBackend()
{
    if (!release_descriptor()) {
        ESP_LOGE(tag, "MAX31865 descriptor destructor shutdown/release failed");
    }
}

Max31865InitializationStatus Max31865TargetBackend::initialize() noexcept
{
    if (!release_descriptor()) {
        return Max31865InitializationStatus::DescriptorError;
    }
    if (!valid_configuration()) {
        return Max31865InitializationStatus::InvalidConfiguration;
    }

    descriptor_.standard = driver_standard(configuration_.conversion.standard);
    descriptor_.r_ref = configuration_.conversion.reference_resistance_ohms;
    descriptor_.rtd_nominal = configuration_.rtd_nominal_ohms;
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
        static_cast<void>(release_descriptor());
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

bool Max31865TargetBackend::shutdown() noexcept
{
    return release_descriptor();
}

bool Max31865TargetBackend::release_descriptor() noexcept
{
    configured_ = false;
    readiness_.invalidate();
    if (!descriptor_acquired_) return true;

    const bool quiesced = write_and_verify_exact_configuration(
        configuration_.terminal_configuration
    );
    const esp_err_t release_result = max31865_free_desc(&descriptor_);
    if (release_result == ESP_OK) {
        descriptor_acquired_ = false;
    } else {
        ESP_LOGE(tag, "MAX31865 descriptor release failed: %s", esp_err_to_name(release_result));
    }
    return quiesced && release_result == ESP_OK;
}

bool Max31865TargetBackend::configure_device() noexcept
{
    readiness_.invalidate();
    // Driver 1.0.8's setter is a read-modify-write which can preserve D5,
    // D3:D2, or D1 command bits. Production establishes the complete exact
    // persistent command-zero byte and verifies its readback instead.
    if (!write_and_verify_exact_configuration(
            configuration_.active_configuration
        )) {
        return false;
    }
    readiness_.continuous_configuration_applied();
    return true;
}

void Max31865TargetBackend::clear_fault_for_later_read() noexcept
{
    // The faulting sample remains absent. Use an exact command write, then
    // re-establish and verify the exact persistent active byte. Successful
    // reconfiguration arms a new conversion boundary; no old sample is reused.
    readiness_.invalidate();
    configured_ = false;
    const std::uint8_t clear_command = static_cast<std::uint8_t>(
        configuration_.active_configuration | fault_clear_command_bit
    );
    if (write_exact_configuration(clear_command) != ESP_OK
        || !configure_device()) {
        configured_ = false;
        return;
    }
    configured_ = true;
}

bool Max31865TargetBackend::valid_configuration() const noexcept
{
    return configuration_.spi_host == max31865_spi_host
        && bus_owner_ != nullptr
        && bus_owner_->owns_initialized_bus(configuration_.spi_host)
        && configuration_.chip_select_gpio == max31865_chip_select_gpio
        && GPIO_IS_VALID_OUTPUT_GPIO(configuration_.chip_select_gpio)
        && configuration_.clock_speed_hz > 0U
        && configuration_.clock_speed_hz <= MAX31865_MAX_CLOCK_SPEED_HZ
        && configuration_.rtd_nominal_ohms == max31865_pt100_nominal_ohms
        && configuration_.connection == MAX31865_3WIRE
        && valid_max31865_temperature_validity_policy(
            configuration_.temperature_validity
        )
        && configuration_.active_configuration
            == static_cast<std::uint8_t>(
                0xC0U | 0x10U
                | (configuration_.conversion.filter == Max31865FilterFrequency::Hz50
                    ? 0x01U
                    : 0x00U)
            )
        && configuration_.terminal_configuration
            == static_cast<std::uint8_t>(
                0x10U
                | (configuration_.conversion.filter == Max31865FilterFrequency::Hz50
                    ? 0x01U
                    : 0x00U)
            )
        && valid_max31865_conversion_configuration(configuration_.conversion);
}

esp_err_t Max31865TargetBackend::write_exact_configuration(
    const std::uint8_t value
) noexcept
{
    spi_transaction_t transaction{};
    const std::uint8_t transmit[]{
        static_cast<std::uint8_t>(configuration_register | write_register_mask),
        value,
    };
    transaction.tx_buffer = transmit;
    transaction.length = sizeof(transmit) * 8U;
    return spi_device_transmit(descriptor_.spi_dev, &transaction);
}

esp_err_t Max31865TargetBackend::read_exact_configuration(
    std::uint8_t& value
) noexcept
{
    spi_transaction_t transaction{};
    const std::uint8_t transmit[]{configuration_register, 0U};
    std::uint8_t receive[sizeof(transmit)]{};
    transaction.tx_buffer = transmit;
    transaction.rx_buffer = receive;
    transaction.length = sizeof(transmit) * 8U;
    const esp_err_t result = spi_device_transmit(descriptor_.spi_dev, &transaction);
    if (result == ESP_OK) value = receive[1];
    return result;
}

bool Max31865TargetBackend::write_and_verify_exact_configuration(
    const std::uint8_t value
) noexcept
{
    if (write_exact_configuration(value) != ESP_OK) return false;
    std::uint8_t observed = 0U;
    return read_exact_configuration(observed) == ESP_OK && observed == value;
}

} // namespace smoker::platform

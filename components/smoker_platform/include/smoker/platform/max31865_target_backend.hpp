#pragma once

#include "smoker/platform/max31865_sensor.hpp"
#include "smoker/platform/max31865_spi_bus.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "max31865.h"

#include <cstdint>

namespace smoker::platform {

// Bus ownership is external and must already be initialized. Every conversion
// and exact-register value is explicit; production supplies the evidence-
// classified values from max31865_production_configuration.hpp.
struct Max31865TargetConfiguration final {
    spi_host_device_t spi_host;
    gpio_num_t chip_select_gpio;
    std::uint32_t clock_speed_hz;
    float rtd_nominal_ohms;
    max31865_connection_type_t connection;
    Max31865ConversionConfiguration conversion;
    Max31865TemperatureValidityPolicy temperature_validity;
    std::uint8_t active_configuration;
    std::uint8_t terminal_configuration;
};

class Max31865TargetBackend final : public IMax31865Backend {
public:
    explicit Max31865TargetBackend(
        Max31865TargetConfiguration configuration,
        const app::IClock& clock,
        const Max31865SpiBusOwner* bus_owner
    ) noexcept;
    ~Max31865TargetBackend() override;

    Max31865TargetBackend(const Max31865TargetBackend&) = delete;
    Max31865TargetBackend& operator=(const Max31865TargetBackend&) = delete;
    Max31865TargetBackend(Max31865TargetBackend&&) = delete;
    Max31865TargetBackend& operator=(Max31865TargetBackend&&) = delete;

    [[nodiscard]] Max31865InitializationStatus initialize() noexcept override;
    [[nodiscard]] Max31865ReadResult read_continuous() noexcept override;
    [[nodiscard]] bool shutdown() noexcept;

private:
    [[nodiscard]] bool release_descriptor() noexcept;
    [[nodiscard]] bool configure_device() noexcept;
    void clear_fault_for_later_read() noexcept;
    [[nodiscard]] bool valid_configuration() const noexcept;
    [[nodiscard]] esp_err_t write_exact_configuration(std::uint8_t value) noexcept;
    [[nodiscard]] esp_err_t read_exact_configuration(std::uint8_t& value) noexcept;
    [[nodiscard]] bool write_and_verify_exact_configuration(std::uint8_t value) noexcept;

    Max31865TargetConfiguration configuration_;
    const Max31865SpiBusOwner* bus_owner_;
    max31865_t descriptor_{};
    bool descriptor_acquired_{false};
    bool configured_{false};
    Max31865ReadinessPolicy readiness_;
};

} // namespace smoker::platform

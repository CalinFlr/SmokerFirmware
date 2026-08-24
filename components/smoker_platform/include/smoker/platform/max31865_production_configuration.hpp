#pragma once

#include "smoker/platform/max31865_board_pins.hpp"
#include "smoker/platform/max31865_spi_bus.hpp"
#include "smoker/platform/max31865_target_backend.hpp"

#include <chrono>
#include <cstdint>

namespace smoker::platform {

struct Max31865ProductionConfiguration final {
    Max31865SpiBusConfiguration bus;
    Max31865TargetConfiguration sensor;
    core::Duration first_sample_boundary;
};

// Production configuration is intentionally centralized here rather than
// hidden as generic adapter defaults.
//
// T-pass connected evidence: SPI2/GPIO12/11/13/10, mode 1 through pinned
// driver 1.0.8, 100 kHz, three-wire/50 Hz, exact active 0xD1 and terminal 0x11.
// Maintainer/probe documentation: PT100 nominal 100.0 ohm and three-wire.
// Supplier documentation/operational validity: inclusive -50.0..+200.0 C
// assembled-probe range. This is not measured calibration.
// Provisional operational choice: ITS-90 and Rref 430.0 ohm produced the
// corroborated approximately 31.3 C result. This is not a measurement of the
// physical reference resistor, its tolerance, or calibrated accuracy.
inline constexpr Max31865ProductionConfiguration max31865_production_configuration{
    Max31865SpiBusConfiguration{
        max31865_spi_host,
        max31865_sck_gpio,
        max31865_mosi_gpio,
        max31865_miso_gpio,
        GPIO_PULLUP_ONLY,
        3,
    },
    Max31865TargetConfiguration{
        max31865_spi_host,
        max31865_chip_select_gpio,
        100'000U,
        100.0F,
        MAX31865_3WIRE,
        Max31865ConversionConfiguration{
            430.0F,
            Max31865FilterFrequency::Hz50,
            Max31865RtdStandard::Its90,
        },
        Max31865TemperatureValidityPolicy{-50.0F, 200.0F},
        0xD1U,
        0x11U,
    },
    std::chrono::milliseconds{66},
};

static_assert(max31865_production_configuration.bus.spi_host == SPI2_HOST);
static_assert(max31865_production_configuration.bus.sck_gpio == GPIO_NUM_12);
static_assert(max31865_production_configuration.bus.mosi_gpio == GPIO_NUM_11);
static_assert(max31865_production_configuration.bus.miso_gpio == GPIO_NUM_13);
static_assert(max31865_production_configuration.bus.miso_pull_mode == GPIO_PULLUP_ONLY);
static_assert(max31865_production_configuration.sensor.chip_select_gpio == GPIO_NUM_10);
static_assert(max31865_production_configuration.sensor.clock_speed_hz == 100'000U);
static_assert(max31865_production_configuration.sensor.rtd_nominal_ohms == 100.0F);
static_assert(max31865_production_configuration.sensor.connection == MAX31865_3WIRE);
static_assert(
    max31865_production_configuration.sensor.temperature_validity.minimum_celsius
    == -50.0F
);
static_assert(
    max31865_production_configuration.sensor.temperature_validity.maximum_celsius
    == 200.0F
);
static_assert(
    max31865_production_configuration.sensor.conversion.reference_resistance_ohms
    == 430.0F
);
static_assert(
    max31865_production_configuration.sensor.conversion.filter
    == Max31865FilterFrequency::Hz50
);
static_assert(
    max31865_production_configuration.sensor.conversion.standard
    == Max31865RtdStandard::Its90
);
static_assert(max31865_production_configuration.sensor.active_configuration == 0xD1U);
static_assert(max31865_production_configuration.sensor.terminal_configuration == 0x11U);
static_assert(
    max31865_production_configuration.first_sample_boundary
    == std::chrono::milliseconds{66}
);

} // namespace smoker::platform

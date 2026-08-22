#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace smoker::platform {

// Final MAX31865 production wiring on the soldered KFB003 controller board,
// confirmed by the maintainer on 2026-08-22. Electrical behavior remains an
// M6B/M7 validation item; these assignments alone do not activate the sensor.
inline constexpr spi_host_device_t max31865_spi_host = SPI2_HOST;
inline constexpr gpio_num_t max31865_sck_gpio = GPIO_NUM_12;
inline constexpr gpio_num_t max31865_mosi_gpio = GPIO_NUM_11;
inline constexpr gpio_num_t max31865_miso_gpio = GPIO_NUM_13;
inline constexpr gpio_num_t max31865_chip_select_gpio = GPIO_NUM_10;

} // namespace smoker::platform

#include "smoker/platform/max31865_spi_bus.hpp"

#include "esp_err.h"
#include "esp_log.h"

namespace smoker::platform {
namespace {

constexpr char tag[] = "smoker_v0";

} // namespace

Max31865SpiBusOwner::Max31865SpiBusOwner(
    const Max31865SpiBusConfiguration configuration
) noexcept
    : configuration_{configuration}
{
}

Max31865SpiBusOwner::~Max31865SpiBusOwner()
{
    if (!release()) {
        ESP_LOGE(tag, "MAX31865 SPI bus destructor release failed");
    }
}

bool Max31865SpiBusOwner::initialize() noexcept
{
    if (initialized()) return true;
    if (bus_owned_ || miso_pull_owned_) {
        ESP_LOGE(tag, "MAX31865 SPI bus has incomplete prior ownership; initialization rejected");
        return false;
    }
    if (configuration_.miso_pull_mode != GPIO_PULLUP_ONLY) {
        ESP_LOGE(tag, "MAX31865 SPI bus requires an explicit MISO pull-up");
        return false;
    }

    spi_bus_config_t bus_configuration{};
    bus_configuration.mosi_io_num = configuration_.mosi_gpio;
    bus_configuration.miso_io_num = configuration_.miso_gpio;
    bus_configuration.sclk_io_num = configuration_.sck_gpio;
    bus_configuration.quadwp_io_num = -1;
    bus_configuration.quadhd_io_num = -1;
    bus_configuration.max_transfer_sz = configuration_.maximum_transfer_bytes;
    const esp_err_t result = spi_bus_initialize(
        configuration_.spi_host,
        &bus_configuration,
        SPI_DMA_DISABLED
    );
    if (result != ESP_OK) {
        ESP_LOGE(tag, "MAX31865 SPI bus initialization failed: %s", esp_err_to_name(result));
        return false;
    }
    bus_owned_ = true;

    // ESP-IDF routes MISO during spi_bus_initialize(), but does not select a
    // deterministic pull. Keep disconnected/high-impedance SDO biased high for
    // the complete owned bus lifetime.
    const esp_err_t pull_result = gpio_set_pull_mode(
        configuration_.miso_gpio, configuration_.miso_pull_mode
    );
    if (pull_result != ESP_OK) {
        ESP_LOGE(tag, "MAX31865 MISO pull setup failed: %s", esp_err_to_name(pull_result));
        miso_pull_owned_ = true;
        static_cast<void>(release());
        return false;
    }
    miso_pull_owned_ = true;
    return true;
}

bool Max31865SpiBusOwner::release() noexcept
{
    bool released = true;
    if (bus_owned_) {
        const esp_err_t result = spi_bus_free(configuration_.spi_host);
        if (result != ESP_OK) {
            ESP_LOGE(tag, "MAX31865 SPI bus release failed: %s", esp_err_to_name(result));
            return false;
        }
        bus_owned_ = false;
    }
    if (miso_pull_owned_) {
        const esp_err_t pull_result = gpio_set_pull_mode(
            configuration_.miso_gpio, GPIO_FLOATING
        );
        if (pull_result != ESP_OK) {
            ESP_LOGE(tag, "MAX31865 MISO pull cleanup failed: %s", esp_err_to_name(pull_result));
            released = false;
        } else {
            miso_pull_owned_ = false;
        }
    }
    return released;
}

bool Max31865SpiBusOwner::initialized() const noexcept
{
    return bus_owned_
        && miso_pull_owned_
        && configuration_.miso_pull_mode == GPIO_PULLUP_ONLY;
}

bool Max31865SpiBusOwner::owns_initialized_bus(
    const spi_host_device_t spi_host
) const noexcept
{
    return initialized() && configuration_.spi_host == spi_host;
}

} // namespace smoker::platform

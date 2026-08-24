#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace smoker::platform {

struct Max31865SpiBusConfiguration final {
    spi_host_device_t spi_host;
    gpio_num_t sck_gpio;
    gpio_num_t mosi_gpio;
    gpio_num_t miso_gpio;
    gpio_pull_mode_t miso_pull_mode;
    int maximum_transfer_bytes;
};

// Target-only owner for the production sensor bus. A runtime context must own
// this object before constructing the MAX31865 descriptor and must destroy the
// descriptor before this owner.
class Max31865SpiBusOwner final {
public:
    explicit Max31865SpiBusOwner(
        Max31865SpiBusConfiguration configuration
    ) noexcept;
    ~Max31865SpiBusOwner();

    Max31865SpiBusOwner(const Max31865SpiBusOwner&) = delete;
    Max31865SpiBusOwner& operator=(const Max31865SpiBusOwner&) = delete;
    Max31865SpiBusOwner(Max31865SpiBusOwner&&) = delete;
    Max31865SpiBusOwner& operator=(Max31865SpiBusOwner&&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool release() noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool owns_initialized_bus(spi_host_device_t spi_host) const noexcept;

private:
    Max31865SpiBusConfiguration configuration_;
    bool bus_owned_{false};
    bool miso_pull_owned_{false};
};

} // namespace smoker::platform

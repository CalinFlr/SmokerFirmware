#pragma once

#include "smoker/platform/max31865_sensor.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "max31865.h"

#include <cstdint>

namespace smoker::platform {

// Bus ownership/initialization and conversion values are supplied by a future
// M6B-backed composition. The backend accepts only the final board host/CS
// assignment from max31865_board_pins.hpp; clock/Rref/filter/standard remain
// explicit with no fabricated defaults.
struct Max31865TargetConfiguration final {
    spi_host_device_t spi_host;
    gpio_num_t chip_select_gpio;
    std::uint32_t clock_speed_hz;
    Max31865ConversionConfiguration conversion;
};

class Max31865TargetBackend final : public IMax31865Backend {
public:
    explicit Max31865TargetBackend(
        Max31865TargetConfiguration configuration,
        const app::IClock& clock
    ) noexcept;
    ~Max31865TargetBackend() override;

    Max31865TargetBackend(const Max31865TargetBackend&) = delete;
    Max31865TargetBackend& operator=(const Max31865TargetBackend&) = delete;
    Max31865TargetBackend(Max31865TargetBackend&&) = delete;
    Max31865TargetBackend& operator=(Max31865TargetBackend&&) = delete;

    [[nodiscard]] Max31865InitializationStatus initialize() noexcept override;
    [[nodiscard]] Max31865ReadResult read_continuous() noexcept override;

private:
    void release_descriptor() noexcept;
    [[nodiscard]] bool configure_device() noexcept;
    void clear_fault_for_later_read() noexcept;
    [[nodiscard]] bool valid_configuration() const noexcept;

    Max31865TargetConfiguration configuration_;
    max31865_t descriptor_{};
    bool descriptor_acquired_{false};
    bool configured_{false};
    Max31865ReadinessPolicy readiness_;
};

} // namespace smoker::platform

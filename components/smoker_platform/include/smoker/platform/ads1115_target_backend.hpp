#pragma once

#include "smoker/platform/ads1115_food_probe_source.hpp"
#include "smoker/platform/i2cdev_subsystem.hpp"

#include <ads111x.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace smoker::platform {

class Ads1115TargetBackend final : public IAds1115Backend {
public:
    explicit Ads1115TargetBackend(I2cdevSubsystemOwner& subsystem) noexcept;
    ~Ads1115TargetBackend() override;

    Ads1115TargetBackend(const Ads1115TargetBackend&) = delete;
    Ads1115TargetBackend& operator=(const Ads1115TargetBackend&) = delete;
    Ads1115TargetBackend(Ads1115TargetBackend&&) = delete;
    Ads1115TargetBackend& operator=(Ads1115TargetBackend&&) = delete;

    [[nodiscard]] bool initialize(
        std::span<const Ads1115DeviceConfiguration> devices
    ) noexcept override;
    [[nodiscard]] bool configure_channel(
        const Ads1115ChannelConfiguration& channel
    ) noexcept override;
    [[nodiscard]] bool start_conversion(std::size_t device_index) noexcept override;
    [[nodiscard]] bool conversion_busy(
        std::size_t device_index,
        bool& busy
    ) noexcept override;
    [[nodiscard]] bool get_value(
        std::size_t device_index,
        std::int16_t& raw_value
    ) noexcept override;
    [[nodiscard]] bool shutdown() noexcept;

private:
    [[nodiscard]] bool valid_device_configuration(
        const Ads1115DeviceConfiguration& device
    ) const noexcept;
    I2cdevSubsystemOwner& subsystem_;
    std::array<i2c_dev_t, 2U> descriptors_{};
    std::array<bool, 2U> descriptor_acquired_{};
    std::size_t descriptor_count_{0U};
    bool descriptor_owner_acquired_{false};
};

} // namespace smoker::platform

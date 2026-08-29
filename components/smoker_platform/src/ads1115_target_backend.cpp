#include "smoker/platform/ads1115_target_backend.hpp"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

#include <optional>

namespace smoker::platform {
namespace {

constexpr std::uint32_t ads1115_minimum_clock_hz = 10'000U;
constexpr std::uint32_t ads1115_driver_maximum_clock_hz = 1'000'000U;

[[nodiscard]] std::optional<ads111x_mux_t> driver_mux(
    const Ads1115Mux mux
) noexcept
{
    switch (mux) {
    case Ads1115Mux::Differential0To1: return ADS111X_MUX_0_1;
    case Ads1115Mux::Differential0To3: return ADS111X_MUX_0_3;
    case Ads1115Mux::Differential1To3: return ADS111X_MUX_1_3;
    case Ads1115Mux::Differential2To3: return ADS111X_MUX_2_3;
    case Ads1115Mux::SingleEnded0: return ADS111X_MUX_0_GND;
    case Ads1115Mux::SingleEnded1: return ADS111X_MUX_1_GND;
    case Ads1115Mux::SingleEnded2: return ADS111X_MUX_2_GND;
    case Ads1115Mux::SingleEnded3: return ADS111X_MUX_3_GND;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ads111x_gain_t> driver_gain(
    const Ads1115Gain gain
) noexcept
{
    switch (gain) {
    case Ads1115Gain::FullScale6V144: return ADS111X_GAIN_6V144;
    case Ads1115Gain::FullScale4V096: return ADS111X_GAIN_4V096;
    case Ads1115Gain::FullScale2V048: return ADS111X_GAIN_2V048;
    case Ads1115Gain::FullScale1V024: return ADS111X_GAIN_1V024;
    case Ads1115Gain::FullScale0V512: return ADS111X_GAIN_0V512;
    case Ads1115Gain::FullScale0V256: return ADS111X_GAIN_0V256;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ads111x_data_rate_t> driver_data_rate(
    const Ads1115DataRate data_rate
) noexcept
{
    switch (data_rate) {
    case Ads1115DataRate::SamplesPerSecond8: return ADS111X_DATA_RATE_8;
    case Ads1115DataRate::SamplesPerSecond16: return ADS111X_DATA_RATE_16;
    case Ads1115DataRate::SamplesPerSecond32: return ADS111X_DATA_RATE_32;
    case Ads1115DataRate::SamplesPerSecond64: return ADS111X_DATA_RATE_64;
    case Ads1115DataRate::SamplesPerSecond128: return ADS111X_DATA_RATE_128;
    case Ads1115DataRate::SamplesPerSecond250: return ADS111X_DATA_RATE_250;
    case Ads1115DataRate::SamplesPerSecond475: return ADS111X_DATA_RATE_475;
    case Ads1115DataRate::SamplesPerSecond860: return ADS111X_DATA_RATE_860;
    }
    return std::nullopt;
}

} // namespace

Ads1115TargetBackend::Ads1115TargetBackend(
    I2cdevSubsystemOwner& subsystem
) noexcept
    : subsystem_{subsystem}
{
}

Ads1115TargetBackend::~Ads1115TargetBackend()
{
    static_cast<void>(shutdown());
}

bool Ads1115TargetBackend::initialize(
    const std::span<const Ads1115DeviceConfiguration> devices
) noexcept
{
    if (descriptor_owner_acquired_
        || descriptor_count_ != 0U
        || devices.empty()
        || devices.size() > descriptors_.size()
        || !subsystem_.active()) {
        return false;
    }
    for (const auto& configuration : devices) {
        if (!valid_device_configuration(configuration)) return false;
    }
    if (!subsystem_.acquire_descriptor_owner()) return false;
    descriptor_owner_acquired_ = true;
    descriptor_count_ = devices.size();

    for (std::size_t index = 0U; index < devices.size(); ++index) {
        const auto& configuration = devices[index];
        auto& descriptor = descriptors_[index];
        descriptor = {};
        if (ads111x_init_desc(
                &descriptor,
                configuration.address,
                static_cast<i2c_port_t>(configuration.i2c_port),
                static_cast<gpio_num_t>(configuration.sda_gpio),
                static_cast<gpio_num_t>(configuration.scl_gpio)
            ) != ESP_OK) {
            static_cast<void>(shutdown());
            return false;
        }
        descriptor_acquired_[index] = true;

        // ads111x_init_desc() installs its driver-owned 1 MHz value. The
        // project-required clock and explicit pull-up policy must replace it
        // before set_mode() performs the first I2C transaction/lazy bus setup.
        descriptor.cfg.master.clk_speed = configuration.clock_speed_hz;
        const auto internal_pullups = static_cast<std::uint8_t>(
            configuration.pullup_policy == Ads1115PullupPolicy::Internal
        );
        descriptor.cfg.sda_pullup_en = internal_pullups;
        descriptor.cfg.scl_pullup_en = internal_pullups;

        if (ads111x_set_mode(&descriptor, ADS111X_MODE_SINGLE_SHOT) != ESP_OK) {
            static_cast<void>(shutdown());
            return false;
        }
    }
    return true;
}

bool Ads1115TargetBackend::configure_channel(
    const Ads1115ChannelConfiguration& channel
) noexcept
{
    if (channel.device_index >= descriptor_count_
        || !descriptor_acquired_[channel.device_index]) {
        return false;
    }
    const auto mux = driver_mux(channel.mux);
    const auto gain = driver_gain(channel.gain);
    const auto rate = driver_data_rate(channel.data_rate);
    if (!mux || !gain || !rate) return false;

    auto* const descriptor = &descriptors_[channel.device_index];
    return ads111x_set_input_mux(descriptor, *mux) == ESP_OK
        && ads111x_set_gain(descriptor, *gain) == ESP_OK
        && ads111x_set_data_rate(descriptor, *rate) == ESP_OK;
}

bool Ads1115TargetBackend::start_conversion(
    const std::size_t device_index
) noexcept
{
    return device_index < descriptor_count_
        && descriptor_acquired_[device_index]
        && ads111x_start_conversion(&descriptors_[device_index]) == ESP_OK;
}

bool Ads1115TargetBackend::conversion_busy(
    const std::size_t device_index,
    bool& busy
) noexcept
{
    return device_index < descriptor_count_
        && descriptor_acquired_[device_index]
        && ads111x_is_busy(&descriptors_[device_index], &busy) == ESP_OK;
}

bool Ads1115TargetBackend::get_value(
    const std::size_t device_index,
    std::int16_t& raw_value
) noexcept
{
    return device_index < descriptor_count_
        && descriptor_acquired_[device_index]
        && ads111x_get_value(&descriptors_[device_index], &raw_value) == ESP_OK;
}

bool Ads1115TargetBackend::valid_device_configuration(
    const Ads1115DeviceConfiguration& device
) const noexcept
{
    return device.i2c_port >= 0
        && device.i2c_port < I2C_NUM_MAX
        && GPIO_IS_VALID_OUTPUT_GPIO(device.sda_gpio)
        && GPIO_IS_VALID_OUTPUT_GPIO(device.scl_gpio)
        && device.sda_gpio != device.scl_gpio
        && device.clock_speed_hz >= ads1115_minimum_clock_hz
        && device.clock_speed_hz <= ads1115_driver_maximum_clock_hz
        && (device.pullup_policy == Ads1115PullupPolicy::External
            || device.pullup_policy == Ads1115PullupPolicy::Internal)
        && device.address >= ADS111X_ADDR_GND
        && device.address <= ADS111X_ADDR_SCL;
}

bool Ads1115TargetBackend::shutdown() noexcept
{
    bool all_released = true;
    for (std::size_t index = descriptor_count_; index > 0U; --index) {
        const auto descriptor_index = index - 1U;
        if (!descriptor_acquired_[descriptor_index]) continue;
        // ESP_OK proves only that ads111x_free_desc() returned ESP_OK. Locked
        // i2cdev 2.1.2 can log and swallow nested device/bus deletion failures,
        // clear their handles, and still return ESP_OK from this call.
        if (ads111x_free_desc(&descriptors_[descriptor_index]) == ESP_OK) {
            descriptor_acquired_[descriptor_index] = false;
        } else {
            all_released = false;
        }
    }
    if (all_released) {
        descriptor_count_ = 0U;
        if (descriptor_owner_acquired_) {
            subsystem_.release_descriptor_owner();
            descriptor_owner_acquired_ = false;
        }
    }
    return all_released;
}

} // namespace smoker::platform

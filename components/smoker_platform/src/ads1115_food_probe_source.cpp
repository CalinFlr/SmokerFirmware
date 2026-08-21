#include "smoker/platform/ads1115_food_probe_source.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace smoker::platform {
namespace {

constexpr std::size_t ads1115_device_count = 2U;
constexpr std::uint32_t ads1115_minimum_clock_hz = 10'000U;
constexpr std::uint32_t ads1115_driver_maximum_clock_hz = 1'000'000U;

[[nodiscard]] constexpr std::uint32_t samples_per_second(
    const Ads1115DataRate data_rate
) noexcept
{
    switch (data_rate) {
    case Ads1115DataRate::SamplesPerSecond8: return 8U;
    case Ads1115DataRate::SamplesPerSecond16: return 16U;
    case Ads1115DataRate::SamplesPerSecond32: return 32U;
    case Ads1115DataRate::SamplesPerSecond64: return 64U;
    case Ads1115DataRate::SamplesPerSecond128: return 128U;
    case Ads1115DataRate::SamplesPerSecond250: return 250U;
    case Ads1115DataRate::SamplesPerSecond475: return 475U;
    case Ads1115DataRate::SamplesPerSecond860: return 860U;
    }
    return 0U;
}

[[nodiscard]] constexpr bool valid_pullup_policy(
    const Ads1115PullupPolicy policy
) noexcept
{
    return policy == Ads1115PullupPolicy::External
        || policy == Ads1115PullupPolicy::Internal;
}

[[nodiscard]] constexpr bool valid_mux(const Ads1115Mux mux) noexcept
{
    return static_cast<std::uint8_t>(mux)
        <= static_cast<std::uint8_t>(Ads1115Mux::SingleEnded3);
}

[[nodiscard]] constexpr bool valid_gain(const Ads1115Gain gain) noexcept
{
    return static_cast<std::uint8_t>(gain)
        <= static_cast<std::uint8_t>(Ads1115Gain::FullScale0V256);
}

[[nodiscard]] bool valid_device(
    const Ads1115DeviceConfiguration& device
) noexcept
{
    return device.i2c_port >= 0
        && device.sda_gpio >= 0
        && device.scl_gpio >= 0
        && device.sda_gpio != device.scl_gpio
        && device.clock_speed_hz >= ads1115_minimum_clock_hz
        && device.clock_speed_hz <= ads1115_driver_maximum_clock_hz
        && valid_pullup_policy(device.pullup_policy)
        && device.address >= 0x48U
        && device.address <= 0x4bU;
}

[[nodiscard]] bool compatible_device_pair(
    const Ads1115DeviceConfiguration& first,
    const Ads1115DeviceConfiguration& second
) noexcept
{
    if (first.i2c_port == second.i2c_port) {
        return first.sda_gpio == second.sda_gpio
            && first.scl_gpio == second.scl_gpio
            && first.clock_speed_hz == second.clock_speed_hz
            && first.pullup_policy == second.pullup_policy
            && first.address != second.address;
    }

    return first.sda_gpio != second.sda_gpio
        && first.sda_gpio != second.scl_gpio
        && first.scl_gpio != second.sda_gpio
        && first.scl_gpio != second.scl_gpio;
}

} // namespace

Ads1115DeviceConfiguration::Ads1115DeviceConfiguration(
    const int i2c_port_value,
    const int sda_gpio_value,
    const int scl_gpio_value,
    const std::uint32_t clock_speed_hz_value,
    const Ads1115PullupPolicy pullup_policy_value,
    const std::uint8_t address_value
) noexcept
    : i2c_port{i2c_port_value}
    , sda_gpio{sda_gpio_value}
    , scl_gpio{scl_gpio_value}
    , clock_speed_hz{clock_speed_hz_value}
    , pullup_policy{pullup_policy_value}
    , address{address_value}
{
}

Ads1115ChannelConfiguration::Ads1115ChannelConfiguration(
    const core::ProbeId probe_id_value,
    const std::size_t device_index_value,
    const Ads1115Mux mux_value,
    const Ads1115Gain gain_value,
    const Ads1115DataRate data_rate_value
) noexcept
    : probe_id{probe_id_value}
    , device_index{device_index_value}
    , mux{mux_value}
    , gain{gain_value}
    , data_rate{data_rate_value}
{
}

Ads1115AcquisitionConfiguration::Ads1115AcquisitionConfiguration(
    std::vector<Ads1115DeviceConfiguration> devices,
    std::vector<Ads1115ChannelConfiguration> channels,
    const core::Duration conversion_timeout,
    const core::Duration sample_maximum_age
) noexcept
    : devices_{std::move(devices)}
    , channels_{std::move(channels)}
    , conversion_timeout_{conversion_timeout}
    , sample_maximum_age_{sample_maximum_age}
{
}

std::span<const Ads1115DeviceConfiguration>
Ads1115AcquisitionConfiguration::devices() const noexcept
{
    return devices_;
}

std::span<const Ads1115ChannelConfiguration>
Ads1115AcquisitionConfiguration::channels() const noexcept
{
    return channels_;
}

core::Duration Ads1115AcquisitionConfiguration::conversion_timeout() const noexcept
{
    return conversion_timeout_;
}

core::Duration Ads1115AcquisitionConfiguration::sample_maximum_age() const noexcept
{
    return sample_maximum_age_;
}

core::Duration minimum_ads1115_conversion_timeout(
    const Ads1115DataRate data_rate
) noexcept
{
    const auto rate = samples_per_second(data_rate);
    if (rate == 0U) return core::Duration::max();

    // TI specifies conversion time as 1 / DR and data-rate variation as +/-10%.
    // This is the ceiling at the documented -10% rate, excluding the separately
    // explicit scheduling and approximately-25-us single-shot power-up margin.
    constexpr std::uint32_t numerator = 10'000U;
    const auto denominator = 9U * rate;
    const auto milliseconds = (numerator + denominator - 1U) / denominator;
    return core::Duration{milliseconds};
}

bool valid_ads1115_acquisition_configuration(
    const Ads1115AcquisitionConfiguration& configuration
) noexcept
{
    const auto devices = configuration.devices();
    const auto channels = configuration.channels();
    if (devices.size() != ads1115_device_count || channels.empty()) return false;
    if (!std::all_of(devices.begin(), devices.end(), valid_device)) return false;
    if (!compatible_device_pair(devices[0], devices[1])) return false;
    if (configuration.conversion_timeout() <= core::Duration::zero()
        || configuration.sample_maximum_age() <= core::Duration::zero()) {
        return false;
    }

    std::array<bool, ads1115_device_count> device_has_channel{};
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        const auto& channel = channels[index];
        if (channel.device_index >= devices.size()
            || !valid_mux(channel.mux)
            || !valid_gain(channel.gain)
            || samples_per_second(channel.data_rate) == 0U
            || configuration.conversion_timeout()
                <= minimum_ads1115_conversion_timeout(channel.data_rate)) {
            return false;
        }
        device_has_channel[channel.device_index] = true;

        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (channels[earlier].probe_id == channel.probe_id
                || (channels[earlier].device_index == channel.device_index
                    && channels[earlier].mux == channel.mux)) {
                return false;
            }
        }
    }
    return std::all_of(
        device_has_channel.begin(),
        device_has_channel.end(),
        [](const bool present) { return present; }
    );
}

Ads1115FoodProbeSource::Ads1115FoodProbeSource(
    Ads1115AcquisitionConfiguration configuration,
    IAds1115Backend& backend,
    IAds1115SampleConverter& sample_converter,
    const app::IClock& clock
)
    : configuration_{std::move(configuration)}
    , backend_{backend}
    , sample_converter_{sample_converter}
    , clock_{clock}
    , samples_(configuration_.channels().size())
{
    configured_ = valid_ads1115_acquisition_configuration(configuration_)
        && backend_.initialize(configuration_.devices());
}

void Ads1115FoodProbeSource::service() noexcept
{
    if (!configured_) return;
    if (state_ == AcquisitionState::Idle) {
        start_next_conversion();
        return;
    }
    poll_active_conversion();
}

std::optional<core::Temperature> Ads1115FoodProbeSource::read(
    const core::ProbeId probe_id
) noexcept
{
    const auto channels = configuration_.channels();
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        if (channels[index].probe_id != probe_id || !samples_[index]) continue;
        const auto now = clock_.now();
        if (now < samples_[index]->observed_at
            || now - samples_[index]->observed_at
                > configuration_.sample_maximum_age()) {
            return std::nullopt;
        }
        return samples_[index]->temperature;
    }
    return std::nullopt;
}

bool Ads1115FoodProbeSource::configured() const noexcept
{
    return configured_;
}

void Ads1115FoodProbeSource::start_next_conversion() noexcept
{
    active_channel_ = next_channel_;
    const auto& channel = configuration_.channels()[active_channel_];
    if (!backend_.configure_channel(channel)
        || !backend_.start_conversion(channel.device_index)) {
        fail_active_probe();
        return;
    }
    conversion_deadline_ = clock_.now() + configuration_.conversion_timeout();
    state_ = AcquisitionState::Converting;
}

void Ads1115FoodProbeSource::poll_active_conversion() noexcept
{
    const auto& channel = configuration_.channels()[active_channel_];
    if (clock_.now() >= conversion_deadline_) {
        fail_active_probe();
        return;
    }

    bool busy = true;
    if (!backend_.conversion_busy(channel.device_index, busy)) {
        fail_active_probe();
        return;
    }
    if (busy) return;

    std::int16_t raw_value = 0;
    if (!backend_.get_value(channel.device_index, raw_value)) {
        fail_active_probe();
        return;
    }

    const auto converted = sample_converter_.convert(channel, raw_value);
    if (converted) {
        samples_[active_channel_] = CachedSample{*converted, clock_.now()};
    } else {
        samples_[active_channel_].reset();
    }
    advance_after_active_conversion();
}

void Ads1115FoodProbeSource::fail_active_probe() noexcept
{
    samples_[active_channel_].reset();
    advance_after_active_conversion();
}

void Ads1115FoodProbeSource::advance_after_active_conversion() noexcept
{
    next_channel_ = (active_channel_ + 1U) % configuration_.channels().size();
    state_ = AcquisitionState::Idle;
}

} // namespace smoker::platform

#pragma once

#include "smoker/app/ports.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace smoker::platform {

enum class Ads1115PullupPolicy : std::uint8_t {
    External,
    Internal,
};

enum class Ads1115Mux : std::uint8_t {
    Differential0To1,
    Differential0To3,
    Differential1To3,
    Differential2To3,
    SingleEnded0,
    SingleEnded1,
    SingleEnded2,
    SingleEnded3,
};

enum class Ads1115Gain : std::uint8_t {
    FullScale6V144,
    FullScale4V096,
    FullScale2V048,
    FullScale1V024,
    FullScale0V512,
    FullScale0V256,
};

enum class Ads1115DataRate : std::uint8_t {
    SamplesPerSecond8,
    SamplesPerSecond16,
    SamplesPerSecond32,
    SamplesPerSecond64,
    SamplesPerSecond128,
    SamplesPerSecond250,
    SamplesPerSecond475,
    SamplesPerSecond860,
};

class Ads1115DeviceConfiguration final {
public:
    Ads1115DeviceConfiguration(
        int i2c_port,
        int sda_gpio,
        int scl_gpio,
        std::uint32_t clock_speed_hz,
        Ads1115PullupPolicy pullup_policy,
        std::uint8_t address
    ) noexcept;

    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    std::uint32_t clock_speed_hz;
    Ads1115PullupPolicy pullup_policy;
    std::uint8_t address;
};

class Ads1115ChannelConfiguration final {
public:
    Ads1115ChannelConfiguration(
        core::ProbeId probe_id,
        std::size_t device_index,
        Ads1115Mux mux,
        Ads1115Gain gain,
        Ads1115DataRate data_rate
    ) noexcept;

    core::ProbeId probe_id;
    std::size_t device_index;
    Ads1115Mux mux;
    Ads1115Gain gain;
    Ads1115DataRate data_rate;
};

class Ads1115AcquisitionConfiguration final {
public:
    Ads1115AcquisitionConfiguration(
        std::vector<Ads1115DeviceConfiguration> devices,
        std::vector<Ads1115ChannelConfiguration> channels,
        core::Duration conversion_timeout,
        core::Duration sample_maximum_age
    ) noexcept;

    [[nodiscard]] std::span<const Ads1115DeviceConfiguration> devices() const noexcept;
    [[nodiscard]] std::span<const Ads1115ChannelConfiguration> channels() const noexcept;
    [[nodiscard]] core::Duration conversion_timeout() const noexcept;
    [[nodiscard]] core::Duration sample_maximum_age() const noexcept;

private:
    std::vector<Ads1115DeviceConfiguration> devices_;
    std::vector<Ads1115ChannelConfiguration> channels_;
    core::Duration conversion_timeout_;
    core::Duration sample_maximum_age_;
};

[[nodiscard]] core::Duration minimum_ads1115_conversion_timeout(
    Ads1115DataRate data_rate
) noexcept;

[[nodiscard]] bool valid_ads1115_acquisition_configuration(
    const Ads1115AcquisitionConfiguration& configuration
) noexcept;

class IAds1115Backend {
public:
    virtual ~IAds1115Backend() = default;
    [[nodiscard]] virtual bool initialize(
        std::span<const Ads1115DeviceConfiguration> devices
    ) noexcept = 0;
    [[nodiscard]] virtual bool configure_channel(
        const Ads1115ChannelConfiguration& channel
    ) noexcept = 0;
    [[nodiscard]] virtual bool start_conversion(std::size_t device_index) noexcept = 0;
    [[nodiscard]] virtual bool conversion_busy(
        std::size_t device_index,
        bool& busy
    ) noexcept = 0;
    [[nodiscard]] virtual bool get_value(
        std::size_t device_index,
        std::int16_t& raw_value
    ) noexcept = 0;
};

class IAds1115SampleConverter {
public:
    virtual ~IAds1115SampleConverter() = default;
    [[nodiscard]] virtual std::optional<core::Temperature> convert(
        const Ads1115ChannelConfiguration& channel,
        std::int16_t raw_value
    ) noexcept = 0;
};

class Ads1115FoodProbeSource final : public app::IFoodProbeSource {
public:
    Ads1115FoodProbeSource(
        Ads1115AcquisitionConfiguration configuration,
        IAds1115Backend& backend,
        IAds1115SampleConverter& sample_converter,
        const app::IClock& clock
    );

    void service() noexcept;
    [[nodiscard]] std::optional<core::Temperature> read(
        core::ProbeId probe_id
    ) noexcept override;
    [[nodiscard]] bool configured() const noexcept;

private:
    struct CachedSample final {
        core::Temperature temperature;
        core::MonotonicTimePoint observed_at;
    };

    enum class AcquisitionState : std::uint8_t {
        Idle,
        Converting,
    };

    void start_next_conversion() noexcept;
    void poll_active_conversion() noexcept;
    void fail_active_probe() noexcept;
    void advance_after_active_conversion() noexcept;

    Ads1115AcquisitionConfiguration configuration_;
    IAds1115Backend& backend_;
    IAds1115SampleConverter& sample_converter_;
    const app::IClock& clock_;
    std::vector<std::optional<CachedSample>> samples_;
    std::size_t next_channel_{0U};
    std::size_t active_channel_{0U};
    core::MonotonicTimePoint conversion_deadline_{};
    AcquisitionState state_{AcquisitionState::Idle};
    bool configured_{false};
};

} // namespace smoker::platform

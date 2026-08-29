#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/ads1115_food_probe_source.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace allocation_probe {

bool enabled = false;
std::size_t allocations = 0U;

void begin() noexcept
{
    allocations = 0U;
    enabled = true;
}

[[nodiscard]] std::size_t end() noexcept
{
    enabled = false;
    return allocations;
}

} // namespace allocation_probe

void* operator new(const std::size_t size)
{
    if (allocation_probe::enabled) ++allocation_probe::allocations;
    if (auto* const memory = std::malloc(size)) return memory;
    std::abort();
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* const memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void* const memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

void operator delete[](void* const memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

namespace {

using smoker::platform::Ads1115AcquisitionConfiguration;
using smoker::platform::Ads1115ChannelConfiguration;
using smoker::platform::Ads1115DataRate;
using smoker::platform::Ads1115DeviceConfiguration;
using smoker::platform::Ads1115FoodProbeSource;
using smoker::platform::Ads1115Gain;
using smoker::platform::Ads1115Mux;
using smoker::platform::Ads1115PullupPolicy;

class FakeMonotonicClock final : public smoker::app::IClock {
public:
    [[nodiscard]] smoker::core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void advance(const smoker::core::Duration duration) noexcept
    {
        now_ += duration;
    }

private:
    smoker::core::MonotonicTimePoint now_{};
};

enum class BackendAction : std::uint8_t {
    Configure,
    Start,
    Busy,
    Get,
};

struct BackendCall final {
    BackendAction action{};
    smoker::core::ProbeId probe_id{0U};
    std::size_t device_index{0U};
    Ads1115Mux mux{Ads1115Mux::Differential0To1};
    Ads1115Gain gain{Ads1115Gain::FullScale6V144};
    Ads1115DataRate data_rate{Ads1115DataRate::SamplesPerSecond8};
};

struct ConversionProvenance final {
    smoker::core::ProbeId probe_id{0U};
    std::size_t device_index{0U};
    Ads1115Mux mux{Ads1115Mux::Differential0To1};
    Ads1115Gain gain{Ads1115Gain::FullScale6V144};
    Ads1115DataRate data_rate{Ads1115DataRate::SamplesPerSecond8};
};

class FakeAds1115Backend final : public smoker::platform::IAds1115Backend {
public:
    [[nodiscard]] bool initialize(
        const std::span<const Ads1115DeviceConfiguration> devices
    ) noexcept override
    {
        ++initialize_calls;
        initialized_device_count = devices.size();
        return initialize_succeeds;
    }

    [[nodiscard]] bool configure_channel(
        const Ads1115ChannelConfiguration& channel
    ) noexcept override
    {
        assert(channel.device_index < initialized_device_count);
        const auto provenance = provenance_of(channel);
        record(BackendAction::Configure, provenance);
        if (fail_configure_probe == channel.probe_id) return false;
        devices_[channel.device_index].configured = provenance;
        return true;
    }

    [[nodiscard]] bool start_conversion(
        const std::size_t device_index
    ) noexcept override
    {
        assert(device_index < initialized_device_count);
        auto& device = devices_[device_index];
        if (!device.configured) return false;
        record(BackendAction::Start, *device.configured);

        // A start presented while OS still reports busy cannot replace the
        // provenance of the conversion already in flight.
        if (device.busy) return false;
        if (fail_start_probe == device.configured->probe_id) return false;

        device.in_flight = device.configured;
        device.busy = true;
        return ambiguous_start_probe != device.configured->probe_id;
    }

    [[nodiscard]] bool conversion_busy(
        const std::size_t device_index,
        bool& busy_result
    ) noexcept override
    {
        assert(device_index < initialized_device_count);
        auto& device = devices_[device_index];
        record(
            BackendAction::Busy,
            device.in_flight.value_or(
                device.configured.value_or(ConversionProvenance{0U, device_index})
            )
        );
        if (fail_busy_device[device_index]) return false;
        busy_result = device.busy;
        return true;
    }

    [[nodiscard]] bool get_value(
        const std::size_t device_index,
        std::int16_t& raw_value
    ) noexcept override
    {
        assert(device_index < initialized_device_count);
        auto& device = devices_[device_index];
        if (!device.in_flight) return false;
        const auto provenance = *device.in_flight;
        record(BackendAction::Get, provenance);
        if (fail_get_probe == provenance.probe_id) return false;
        const auto device_part = static_cast<std::int16_t>(device_index * 100U);
        const auto mux_part = static_cast<std::int16_t>(provenance.mux);
        raw_value = static_cast<std::int16_t>(1000 + device_part + mux_part);
        return true;
    }

    void complete_conversion(const std::size_t device_index) noexcept
    {
        devices_[device_index].busy = false;
    }

    void begin_stale_conversion(
        const std::size_t device_index,
        const ConversionProvenance provenance
    ) noexcept
    {
        devices_[device_index].in_flight = provenance;
        devices_[device_index].busy = true;
    }

    [[nodiscard]] bool device_busy(const std::size_t device_index) const noexcept
    {
        return devices_[device_index].busy;
    }

    [[nodiscard]] std::size_t count_calls(
        const BackendAction action,
        const std::optional<std::size_t> device_index = std::nullopt
    ) const noexcept
    {
        std::size_t count = 0U;
        for (std::size_t index = 0U; index < call_count; ++index) {
            if (calls[index].action == action
                && (!device_index || calls[index].device_index == *device_index)) {
                ++count;
            }
        }
        return count;
    }

    void reset_calls() noexcept
    {
        call_count = 0U;
    }

    bool initialize_succeeds{true};
    std::optional<smoker::core::ProbeId> fail_configure_probe;
    std::optional<smoker::core::ProbeId> fail_start_probe;
    std::optional<smoker::core::ProbeId> ambiguous_start_probe;
    std::array<bool, 2U> fail_busy_device{};
    std::optional<smoker::core::ProbeId> fail_get_probe;
    std::size_t initialize_calls{0U};
    std::size_t initialized_device_count{0U};
    std::array<BackendCall, 64U> calls{};
    std::size_t call_count{0U};

private:
    struct Device final {
        std::optional<ConversionProvenance> configured;
        std::optional<ConversionProvenance> in_flight;
        bool busy{false};
    };

    [[nodiscard]] static ConversionProvenance provenance_of(
        const Ads1115ChannelConfiguration& channel
    ) noexcept
    {
        return ConversionProvenance{
            channel.probe_id,
            channel.device_index,
            channel.mux,
            channel.gain,
            channel.data_rate,
        };
    }

    void record(
        const BackendAction action,
        const ConversionProvenance provenance
    ) noexcept
    {
        assert(call_count < calls.size());
        calls[call_count++] = BackendCall{
            action,
            provenance.probe_id,
            provenance.device_index,
            provenance.mux,
            provenance.gain,
            provenance.data_rate,
        };
    }

    std::array<Device, 2U> devices_{};
};

class FakeSampleConverter final : public smoker::platform::IAds1115SampleConverter {
public:
    [[nodiscard]] std::optional<smoker::core::Temperature> convert(
        const Ads1115ChannelConfiguration& channel,
        const std::int16_t raw_value
    ) noexcept override
    {
        ++calls;
        if (fail_probe == channel.probe_id) return std::nullopt;
        return smoker::core::Temperature::from_celsius(
            static_cast<float>(raw_value) / 10.0F
        );
    }

    std::optional<smoker::core::ProbeId> fail_probe;
    std::size_t calls{0U};
};

[[nodiscard]] std::vector<Ads1115DeviceConfiguration> shared_bus_devices()
{
    return {
        Ads1115DeviceConfiguration{
            0, 8, 9, 400'000U, Ads1115PullupPolicy::External, 0x48U,
        },
        Ads1115DeviceConfiguration{
            0, 8, 9, 400'000U, Ads1115PullupPolicy::External, 0x49U,
        },
    };
}

[[nodiscard]] std::vector<Ads1115ChannelConfiguration> three_channels()
{
    return {
        Ads1115ChannelConfiguration{
            1U,
            0U,
            Ads1115Mux::SingleEnded0,
            Ads1115Gain::FullScale2V048,
            Ads1115DataRate::SamplesPerSecond128,
        },
        Ads1115ChannelConfiguration{
            3U,
            0U,
            Ads1115Mux::SingleEnded1,
            Ads1115Gain::FullScale0V512,
            Ads1115DataRate::SamplesPerSecond64,
        },
        Ads1115ChannelConfiguration{
            2U,
            1U,
            Ads1115Mux::SingleEnded2,
            Ads1115Gain::FullScale1V024,
            Ads1115DataRate::SamplesPerSecond250,
        },
    };
}

[[nodiscard]] std::vector<Ads1115ChannelConfiguration> device_zero_channels()
{
    auto channels = three_channels();
    channels.pop_back();
    return channels;
}

[[nodiscard]] Ads1115AcquisitionConfiguration one_device_configuration()
{
    using namespace std::chrono_literals;
    auto devices = shared_bus_devices();
    devices.pop_back();
    return Ads1115AcquisitionConfiguration{
        std::move(devices), device_zero_channels(), 25ms, 100ms,
    };
}

[[nodiscard]] Ads1115AcquisitionConfiguration valid_configuration()
{
    using namespace std::chrono_literals;
    return Ads1115AcquisitionConfiguration{
        shared_bus_devices(),
        three_channels(),
        25ms,
        100ms,
    };
}

void complete_active_conversion(
    Ads1115FoodProbeSource& source,
    FakeAds1115Backend& backend,
    FakeMonotonicClock& clock,
    const std::size_t device_index
)
{
    using namespace std::chrono_literals;
    backend.complete_conversion(device_index);
    clock.advance(1ms);
    source.service();
}

void test_ads1115_invalid_incomplete_configurations_are_rejected()
{
    using namespace std::chrono_literals;
    using smoker::platform::minimum_ads1115_conversion_timeout;
    using smoker::platform::valid_ads1115_acquisition_configuration;

    const auto valid = valid_configuration();
    assert(valid_ads1115_acquisition_configuration(valid));
    assert(valid_ads1115_acquisition_configuration(one_device_configuration()));
    assert(minimum_ads1115_conversion_timeout(Ads1115DataRate::SamplesPerSecond8) == 139ms);
    assert(minimum_ads1115_conversion_timeout(Ads1115DataRate::SamplesPerSecond860) == 2ms);

    assert(!valid_ads1115_acquisition_configuration({
        {}, device_zero_channels(), 25ms, 100ms,
    }));

    auto three_devices = shared_bus_devices();
    three_devices.push_back(Ads1115DeviceConfiguration{
        0, 8, 9, 400'000U, Ads1115PullupPolicy::External, 0x4aU,
    });
    assert(!valid_ads1115_acquisition_configuration({
        std::move(three_devices), three_channels(), 25ms, 100ms,
    }));

    auto one_device = shared_bus_devices();
    one_device.pop_back();
    assert(!valid_ads1115_acquisition_configuration({
        std::move(one_device), three_channels(), 25ms, 100ms,
    }));

    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), device_zero_channels(), 25ms, 100ms,
    }));
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), {}, 25ms, 100ms,
    }));

    auto same_address = shared_bus_devices();
    same_address[1].address = same_address[0].address;
    assert(!valid_ads1115_acquisition_configuration({
        std::move(same_address), three_channels(), 25ms, 100ms,
    }));
    auto incompatible_bus = shared_bus_devices();
    incompatible_bus[1].clock_speed_hz = 100'000U;
    assert(!valid_ads1115_acquisition_configuration({
        std::move(incompatible_bus), three_channels(), 25ms, 100ms,
    }));

    auto separate_buses = shared_bus_devices();
    separate_buses[1] = Ads1115DeviceConfiguration{
        1, 10, 11, 100'000U, Ads1115PullupPolicy::Internal, 0x48U,
    };
    assert(valid_ads1115_acquisition_configuration({
        std::move(separate_buses), three_channels(), 25ms, 100ms,
    }));

    auto duplicate_probe = three_channels();
    duplicate_probe[1].probe_id = duplicate_probe[0].probe_id;
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), std::move(duplicate_probe), 25ms, 100ms,
    }));
    auto duplicate_channel = three_channels();
    duplicate_channel[1].mux = duplicate_channel[0].mux;
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), std::move(duplicate_channel), 25ms, 100ms,
    }));
    auto bad_index = three_channels();
    bad_index[0].device_index = 2U;
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), std::move(bad_index), 25ms, 100ms,
    }));
    auto bad_rate = three_channels();
    bad_rate[0].data_rate = static_cast<Ads1115DataRate>(255U);
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), std::move(bad_rate), 25ms, 100ms,
    }));
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), three_channels(), 18ms, 100ms,
    }));
    assert(!valid_ads1115_acquisition_configuration({
        shared_bus_devices(), three_channels(), 25ms, 0ms,
    }));

    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource rejected{
        Ads1115AcquisitionConfiguration{
            shared_bus_devices(), three_channels(), 18ms, 100ms,
        },
        backend,
        converter,
        clock,
    };
    assert(!rejected.configured());
    assert(backend.initialize_calls == 0U);
}

void test_ads1115_one_device_sequencer_never_touches_device_one()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        one_device_configuration(), backend, converter, clock,
    };
    assert(source.configured());
    assert(backend.initialize_calls == 1U);
    assert(backend.initialized_device_count == 1U);

    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    source.service();
    complete_active_conversion(source, backend, clock, 0U);

    assert(source.read(1U));
    assert(source.read(3U));
    assert(backend.count_calls(BackendAction::Busy, 1U) == 0U);
    assert(backend.count_calls(BackendAction::Configure, 1U) == 0U);
    assert(backend.count_calls(BackendAction::Start, 1U) == 0U);
    assert(backend.count_calls(BackendAction::Get, 1U) == 0U);
    for (std::size_t index = 0U; index < backend.call_count; ++index) {
        assert(backend.calls[index].device_index == 0U);
    }
}

void test_ads1115_both_devices_require_initial_idle_synchronization()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };
    assert(source.configured());
    assert(backend.initialized_device_count == 2U);

    source.service();
    assert(backend.call_count == 1U);
    assert(backend.calls[0].action == BackendAction::Busy);
    assert(backend.calls[0].device_index == 0U);

    source.service();
    assert(backend.calls[1].action == BackendAction::Configure);
    assert(backend.calls[2].action == BackendAction::Start);
    assert(!source.read(1U));
    complete_active_conversion(source, backend, clock, 0U);
    assert(source.read(1U)->celsius() == 100.4F);

    // The consecutive second channel on device 0 does not need another idle
    // synchronization after the first conversion completed normally.
    source.service();
    assert(backend.calls[backend.call_count - 2U].action == BackendAction::Configure);
    assert(backend.calls[backend.call_count - 2U].probe_id == 3U);
    complete_active_conversion(source, backend, clock, 0U);
    assert(source.read(3U)->celsius() == 100.5F);

    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.calls[backend.call_count - 1U].device_index == 1U);
    const auto calls_after_second_sync = backend.call_count;
    source.service();
    assert(backend.call_count == calls_after_second_sync + 2U);
    assert(backend.calls[calls_after_second_sync].action == BackendAction::Configure);
    assert(backend.calls[calls_after_second_sync + 1U].action == BackendAction::Start);
    complete_active_conversion(source, backend, clock, 1U);
    assert(source.read(2U)->celsius() == 110.6F);
}

void test_ads1115_initial_stale_result_is_discarded_before_later_restart()
{
    FakeAds1115Backend backend;
    backend.begin_stale_conversion(
        0U,
        ConversionProvenance{
            99U,
            0U,
            Ads1115Mux::SingleEnded3,
            Ads1115Gain::FullScale6V144,
            Ads1115DataRate::SamplesPerSecond8,
        }
    );
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    assert(backend.call_count == 1U);
    assert(backend.calls[0].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Configure) == 0U);
    assert(backend.count_calls(BackendAction::Get) == 0U);

    backend.complete_conversion(0U);
    source.service();
    assert(backend.call_count == 2U);
    assert(backend.calls[1].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Configure) == 0U);
    assert(backend.count_calls(BackendAction::Start) == 0U);
    assert(backend.count_calls(BackendAction::Get) == 0U);

    source.service();
    assert(backend.calls[2].action == BackendAction::Configure);
    assert(backend.calls[2].probe_id == 3U);
    assert(backend.calls[3].action == BackendAction::Start);
    assert(backend.count_calls(BackendAction::Get) == 0U);
    complete_active_conversion(source, backend, clock, 0U);
    assert(source.read(3U)->celsius() == 100.5F);
    assert(converter.calls == 1U);
}

void test_ads1115_fake_latches_in_flight_provenance_across_reconfiguration()
{
    FakeAds1115Backend backend;
    const auto devices = shared_bus_devices();
    assert(backend.initialize(devices));
    const auto channels = three_channels();

    assert(backend.configure_channel(channels[0]));
    assert(backend.start_conversion(0U));
    assert(backend.configure_channel(channels[1]));
    assert(!backend.start_conversion(0U));
    backend.complete_conversion(0U);

    bool busy = true;
    assert(backend.conversion_busy(0U, busy));
    assert(!busy);
    std::int16_t raw_value = 0;
    assert(backend.get_value(0U, raw_value));
    assert(raw_value == 1004);
    const auto& get = backend.calls[backend.call_count - 1U];
    assert(get.action == BackendAction::Get);
    assert(get.probe_id == channels[0].probe_id);
    assert(get.mux == channels[0].mux);
    assert(get.gain == channels[0].gain);
    assert(get.data_rate == channels[0].data_rate);
}

void assert_ready_at_or_after_deadline_is_accepted(
    const smoker::core::Duration elapsed
)
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    backend.complete_conversion(0U);
    clock.advance(elapsed);
    source.service();

    assert(backend.calls[backend.call_count - 2U].action == BackendAction::Busy);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Get);
    assert(source.read(1U)->celsius() == 100.4F);
}

void test_ads1115_ready_exactly_at_deadline_is_accepted()
{
    using namespace std::chrono_literals;
    assert_ready_at_or_after_deadline_is_accepted(25ms);
}

void test_ads1115_ready_after_deadline_is_accepted()
{
    using namespace std::chrono_literals;
    assert_ready_at_or_after_deadline_is_accepted(26ms);
}

void test_ads1115_busy_exactly_at_deadline_quarantines_without_read()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    clock.advance(25ms);
    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Get) == 0U);
    assert(!source.read(1U));

    const auto calls_before_quarantined_channel = backend.call_count;
    source.service();
    assert(backend.call_count == calls_before_quarantined_channel + 1U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.calls[backend.call_count - 1U].probe_id == 1U);
    assert(backend.count_calls(BackendAction::Configure) == 1U);
}

void test_ads1115_timed_out_conversion_cannot_be_reconfigured_or_misattributed()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    clock.advance(25ms);
    source.service();

    // Probe 3 is consecutive on the same physical ADC. The abandoned probe-1
    // conversion is still busy, so probe 3 may only observe OS and be skipped.
    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Configure, 0U) == 1U);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);
    assert(!source.read(3U));

    backend.complete_conversion(0U);
    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 1U);
    assert(source.read(2U)->celsius() == 110.6F);

    const auto calls_before_recovery = backend.call_count;
    source.service();
    assert(backend.call_count == calls_before_recovery + 1U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);
    source.service();
    assert(backend.calls[backend.call_count - 2U].action == BackendAction::Configure);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
    complete_active_conversion(source, backend, clock, 0U);
    assert(source.read(1U)->celsius() == 100.4F);
}

void test_ads1115_ambiguously_failed_start_is_quarantined_and_discarded()
{
    FakeAds1115Backend backend;
    backend.ambiguous_start_probe = 1U;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    assert(backend.device_busy(0U));
    assert(!source.read(1U));

    const auto calls_before_quarantine_check = backend.call_count;
    source.service();
    assert(backend.call_count == calls_before_quarantine_check + 1U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Configure, 0U) == 1U);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);

    backend.complete_conversion(0U);
    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 1U);
    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);
}

void test_ads1115_busy_observation_error_uses_the_same_quarantine_boundary()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    backend.fail_busy_device[0] = true;
    source.service();
    assert(!source.read(1U));
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);

    backend.fail_busy_device[0] = false;
    const auto configure_count = backend.count_calls(BackendAction::Configure, 0U);
    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Configure, 0U) == configure_count);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);
}

void test_ads1115_quarantined_device_does_not_block_the_other_adc()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    clock.advance(25ms);
    source.service();
    source.service(); // skip consecutive quarantined device-0 channel
    source.service(); // synchronize device 1
    source.service(); // configure/start device 1
    assert(backend.device_busy(0U));
    assert(backend.device_busy(1U));
    complete_active_conversion(source, backend, clock, 1U);
    assert(source.read(2U)->celsius() == 110.6F);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);
}

void test_ads1115_recovery_requires_idle_and_never_reads_or_restarts_same_step()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    clock.advance(25ms);
    source.service();
    source.service();
    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 1U);

    backend.complete_conversion(0U);
    const auto calls_before_recovery = backend.call_count;
    source.service();
    assert(backend.call_count == calls_before_recovery + 1U);
    assert(backend.calls[calls_before_recovery].action == BackendAction::Busy);
    assert(backend.count_calls(BackendAction::Get, 0U) == 0U);

    source.service();
    assert(backend.calls[backend.call_count - 2U].action == BackendAction::Configure);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
}

enum class InjectedFailure : std::uint8_t {
    Configure,
    Start,
    Busy,
    Get,
    Calibration,
};

void populate_all_probe_caches(
    Ads1115FoodProbeSource& source,
    FakeAds1115Backend& backend,
    FakeMonotonicClock& clock
)
{
    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 1U);
}

void assert_failure_clears_only_affected_probe(const InjectedFailure failure)
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };
    populate_all_probe_caches(source, backend, clock);
    assert(source.read(1U));
    assert(source.read(2U));
    assert(source.read(3U));

    switch (failure) {
    case InjectedFailure::Configure: backend.fail_configure_probe = 1U; break;
    case InjectedFailure::Start: backend.fail_start_probe = 1U; break;
    case InjectedFailure::Busy: backend.fail_busy_device[0] = true; break;
    case InjectedFailure::Get: backend.fail_get_probe = 1U; break;
    case InjectedFailure::Calibration: converter.fail_probe = 1U; break;
    }

    source.service();
    if (failure == InjectedFailure::Busy
        || failure == InjectedFailure::Get
        || failure == InjectedFailure::Calibration) {
        if (failure != InjectedFailure::Busy) backend.complete_conversion(0U);
        source.service();
    }
    assert(!source.read(1U));
    assert(source.read(2U));
    assert(source.read(3U));
}

void test_ads1115_per_probe_failures_clear_only_the_affected_sample()
{
    assert_failure_clears_only_affected_probe(InjectedFailure::Configure);
    assert_failure_clears_only_affected_probe(InjectedFailure::Start);
    assert_failure_clears_only_affected_probe(InjectedFailure::Busy);
    assert_failure_clears_only_affected_probe(InjectedFailure::Get);
    assert_failure_clears_only_affected_probe(InjectedFailure::Calibration);
}

void test_ads1115_idle_configure_get_and_calibration_failures_do_not_quarantine()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    backend.fail_configure_probe = 1U;
    source.service();
    backend.fail_configure_probe.reset();
    const auto busy_count_after_configure_failure = backend.count_calls(BackendAction::Busy, 0U);
    source.service();
    assert(backend.calls[backend.call_count - 2U].probe_id == 3U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
    assert(backend.count_calls(BackendAction::Busy, 0U) == busy_count_after_configure_failure);

    backend.fail_get_probe = 3U;
    backend.complete_conversion(0U);
    source.service();
    assert(!source.read(3U));
    backend.fail_get_probe.reset();

    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 1U);
    const auto busy_count_before_device_zero_restart = backend.count_calls(BackendAction::Busy, 0U);
    source.service();
    assert(backend.calls[backend.call_count - 2U].action == BackendAction::Configure);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
    assert(backend.count_calls(BackendAction::Busy, 0U) == busy_count_before_device_zero_restart);

    converter.fail_probe = 1U;
    backend.complete_conversion(0U);
    source.service();
    assert(!source.read(1U));
    converter.fail_probe.reset();
    const auto busy_count_before_post_calibration_start = backend.count_calls(BackendAction::Busy, 0U);
    source.service();
    assert(backend.calls[backend.call_count - 2U].probe_id == 3U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
    assert(backend.count_calls(BackendAction::Busy, 0U) == busy_count_before_post_calibration_start);
}

void test_ads1115_cached_readings_expire_and_unknown_ids_are_absent()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    source.service();
    complete_active_conversion(source, backend, clock, 0U);
    assert(source.read(1U));
    assert(source.read(3U));
    assert(!source.read(99U));

    clock.advance(99ms);
    assert(source.read(1U));
    assert(source.read(3U));
    clock.advance(1ms);
    assert(!source.read(1U));
    assert(source.read(3U));
    clock.advance(1ms);
    assert(!source.read(3U));
}

void test_ads1115_steady_state_service_and_read_are_observed_allocation_free()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    allocation_probe::begin();
    source.service();
    const auto synchronization_allocations = allocation_probe::end();
    assert(synchronization_allocations == 0U);

    allocation_probe::begin();
    source.service();
    const auto start_allocations = allocation_probe::end();
    assert(start_allocations == 0U);

    backend.complete_conversion(0U);
    allocation_probe::begin();
    source.service();
    const auto poll_allocations = allocation_probe::end();
    assert(poll_allocations == 0U);

    allocation_probe::begin();
    const auto reading = source.read(1U);
    const auto read_allocations = allocation_probe::end();
    assert(reading);
    assert(read_allocations == 0U);
}

[[nodiscard]] smoker::core::Temperature temperature(const float celsius)
{
    const auto value = smoker::core::Temperature::from_celsius(celsius);
    assert(value);
    return *value;
}

void test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock acquisition_clock;
    converter.fail_probe = 1U;
    Ads1115FoodProbeSource food_source{
        valid_configuration(), backend, converter, acquisition_clock,
    };

    smoker::platform::SimulatedChamberSensor chamber{temperature(30.0F)};
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    const std::array probes{
        smoker::core::FoodProbeConfig{
            1U, "Probe 1", smoker::core::ProbeRole::Meat,
            std::nullopt, true, false,
        },
        smoker::core::FoodProbeConfig{
            2U, "Probe 2", smoker::core::ProbeRole::Meat,
            std::nullopt, true, false,
        },
        smoker::core::FoodProbeConfig{
            3U, "Probe 3", smoker::core::ProbeRole::Meat,
            std::nullopt, true, false,
        },
    };
    smoker::platform::DeterministicChamberController chamber_controller;
    smoker::app::SmokerApplication application{
        chamber,
        food_source,
        chamber_controller,
        heater,
        clock,
        events,
        smoker::core::SafetyLimits{temperature(150.0F)},
        probes,
    };
    const smoker::core::Recipe recipe{
        9U,
        "M9 monitoring probes",
        smoker::core::Stage{9U, "M9 stage", temperature(110.0F), std::nullopt},
    };
    assert(application.submit(smoker::app::StartSessionCommand{9U, recipe}));

    application.tick();
    const auto missing_snapshot = application.snapshot();
    assert(!missing_snapshot.active_fault);
    assert(missing_snapshot.heater_demand.percent() == 100.0F);

    food_source.service();
    food_source.service();
    complete_active_conversion(food_source, backend, acquisition_clock, 0U);
    application.tick();
    const auto invalid_snapshot = application.snapshot();
    assert(!invalid_snapshot.active_fault);
    assert(invalid_snapshot.heater_demand.percent() == 100.0F);
    assert(heater.last_demand().percent() == 100.0F);
}

} // namespace

int main()
{
    test_ads1115_invalid_incomplete_configurations_are_rejected();
    test_ads1115_one_device_sequencer_never_touches_device_one();
    test_ads1115_both_devices_require_initial_idle_synchronization();
    test_ads1115_initial_stale_result_is_discarded_before_later_restart();
    test_ads1115_fake_latches_in_flight_provenance_across_reconfiguration();
    test_ads1115_ready_exactly_at_deadline_is_accepted();
    test_ads1115_ready_after_deadline_is_accepted();
    test_ads1115_busy_exactly_at_deadline_quarantines_without_read();
    test_ads1115_timed_out_conversion_cannot_be_reconfigured_or_misattributed();
    test_ads1115_ambiguously_failed_start_is_quarantined_and_discarded();
    test_ads1115_busy_observation_error_uses_the_same_quarantine_boundary();
    test_ads1115_quarantined_device_does_not_block_the_other_adc();
    test_ads1115_recovery_requires_idle_and_never_reads_or_restarts_same_step();
    test_ads1115_per_probe_failures_clear_only_the_affected_sample();
    test_ads1115_idle_configure_get_and_calibration_failures_do_not_quarantine();
    test_ads1115_cached_readings_expire_and_unknown_ids_are_absent();
    test_ads1115_steady_state_service_and_read_are_observed_allocation_free();
    test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control();
    return 0;
}

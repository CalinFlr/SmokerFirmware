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
        record(BackendAction::Configure, channel);
        current_channels[channel.device_index] = &channel;
        return fail_configure_probe != channel.probe_id;
    }

    [[nodiscard]] bool start_conversion(
        const std::size_t device_index
    ) noexcept override
    {
        const auto& channel = *current_channels[device_index];
        record(BackendAction::Start, channel);
        return fail_start_probe != channel.probe_id;
    }

    [[nodiscard]] bool conversion_busy(
        const std::size_t device_index,
        bool& busy_result
    ) noexcept override
    {
        const auto& channel = *current_channels[device_index];
        record(BackendAction::Busy, channel);
        if (fail_busy_probe == channel.probe_id) return false;
        busy_result = busy;
        return true;
    }

    [[nodiscard]] bool get_value(
        const std::size_t device_index,
        std::int16_t& raw_value
    ) noexcept override
    {
        const auto& channel = *current_channels[device_index];
        record(BackendAction::Get, channel);
        if (fail_get_probe == channel.probe_id) return false;
        const auto device_part = static_cast<std::int16_t>(device_index * 100U);
        const auto mux_part = static_cast<std::int16_t>(channel.mux);
        raw_value = static_cast<std::int16_t>(1000 + device_part + mux_part);
        return true;
    }

    void reset_calls() noexcept
    {
        call_count = 0U;
    }

    bool initialize_succeeds{true};
    bool busy{false};
    std::optional<smoker::core::ProbeId> fail_configure_probe;
    std::optional<smoker::core::ProbeId> fail_start_probe;
    std::optional<smoker::core::ProbeId> fail_busy_probe;
    std::optional<smoker::core::ProbeId> fail_get_probe;
    std::size_t initialize_calls{0U};
    std::size_t initialized_device_count{0U};
    std::array<BackendCall, 64U> calls{};
    std::size_t call_count{0U};

private:
    void record(
        const BackendAction action,
        const Ads1115ChannelConfiguration& channel
    ) noexcept
    {
        assert(call_count < calls.size());
        calls[call_count++] = BackendCall{
            action,
            channel.probe_id,
            channel.device_index,
            channel.mux,
            channel.gain,
            channel.data_rate,
        };
    }

    std::array<const Ads1115ChannelConfiguration*, 2U> current_channels{};
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
            2U,
            1U,
            Ads1115Mux::SingleEnded2,
            Ads1115Gain::FullScale1V024,
            Ads1115DataRate::SamplesPerSecond250,
        },
        Ads1115ChannelConfiguration{
            3U,
            0U,
            Ads1115Mux::SingleEnded1,
            Ads1115Gain::FullScale0V512,
            Ads1115DataRate::SamplesPerSecond64,
        },
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

void complete_one_conversion(
    Ads1115FoodProbeSource& source,
    FakeMonotonicClock& clock
)
{
    using namespace std::chrono_literals;
    source.service();
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
    assert(minimum_ads1115_conversion_timeout(Ads1115DataRate::SamplesPerSecond8) == 139ms);
    assert(minimum_ads1115_conversion_timeout(Ads1115DataRate::SamplesPerSecond860) == 2ms);

    auto one_device = shared_bus_devices();
    one_device.pop_back();
    assert(!valid_ads1115_acquisition_configuration({
        std::move(one_device), three_channels(), 25ms, 100ms,
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
    duplicate_channel[2].mux = duplicate_channel[0].mux;
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

void test_ads1115_two_devices_and_channels_are_sequenced_without_reuse()
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
    assert(backend.call_count == 2U);
    assert(backend.calls[0].action == BackendAction::Configure);
    assert(backend.calls[1].action == BackendAction::Start);
    assert(!source.read(1U));

    source.service();
    assert(backend.call_count == 4U);
    assert(backend.calls[2].action == BackendAction::Busy);
    assert(backend.calls[3].action == BackendAction::Get);
    const auto first = source.read(1U);
    assert(first && first->celsius() == 100.4F);

    complete_one_conversion(source, clock);
    complete_one_conversion(source, clock);
    assert(source.read(2U)->celsius() == 110.6F);
    assert(source.read(3U)->celsius() == 100.5F);

    const std::array expected_devices{0U, 1U, 0U};
    const std::array expected_muxes{
        Ads1115Mux::SingleEnded0,
        Ads1115Mux::SingleEnded2,
        Ads1115Mux::SingleEnded1,
    };
    for (std::size_t channel = 0U; channel < expected_devices.size(); ++channel) {
        const auto call = backend.calls[channel * 4U];
        assert(call.action == BackendAction::Configure);
        assert(call.device_index == expected_devices[channel]);
        assert(call.mux == expected_muxes[channel]);
    }
}

void test_ads1115_start_and_read_never_share_a_service_step()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    source.service();
    assert(backend.call_count == 2U);
    for (std::size_t index = 0U; index < backend.call_count; ++index) {
        assert(backend.calls[index].action != BackendAction::Get);
    }
    source.service();
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Get);
}

void test_ads1115_busy_and_stuck_conversion_never_read_or_block()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    backend.busy = true;
    source.service();
    source.service();
    assert(backend.call_count == 3U);
    assert(backend.calls[2].action == BackendAction::Busy);
    assert(!source.read(1U));

    clock.advance(25ms);
    source.service();
    assert(backend.call_count == 3U);
    assert(!source.read(1U));

    backend.busy = false;
    source.service();
    assert(backend.calls[backend.call_count - 1U].probe_id == 2U);
    assert(backend.calls[backend.call_count - 1U].action == BackendAction::Start);
}

void test_ads1115_mux_gain_rate_changes_require_a_new_completed_conversion()
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    complete_one_conversion(source, clock);
    source.service();
    assert(backend.calls[backend.call_count - 2U].probe_id == 2U);
    assert(backend.calls[backend.call_count - 2U].gain == Ads1115Gain::FullScale1V024);
    assert(backend.calls[backend.call_count - 2U].data_rate == Ads1115DataRate::SamplesPerSecond250);
    assert(!source.read(2U));
    source.service();
    assert(source.read(2U));

    source.service();
    assert(backend.calls[backend.call_count - 2U].probe_id == 3U);
    assert(backend.calls[backend.call_count - 2U].gain == Ads1115Gain::FullScale0V512);
    assert(backend.calls[backend.call_count - 2U].data_rate == Ads1115DataRate::SamplesPerSecond64);
    assert(!source.read(3U));
}

enum class InjectedFailure : std::uint8_t {
    Configure,
    Start,
    Busy,
    Get,
    Calibration,
};

void assert_failure_clears_only_affected_probe(const InjectedFailure failure)
{
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };
    complete_one_conversion(source, clock);
    complete_one_conversion(source, clock);
    complete_one_conversion(source, clock);
    assert(source.read(1U));
    assert(source.read(2U));
    assert(source.read(3U));

    switch (failure) {
    case InjectedFailure::Configure: backend.fail_configure_probe = 1U; break;
    case InjectedFailure::Start: backend.fail_start_probe = 1U; break;
    case InjectedFailure::Busy: backend.fail_busy_probe = 1U; break;
    case InjectedFailure::Get: backend.fail_get_probe = 1U; break;
    case InjectedFailure::Calibration: converter.fail_probe = 1U; break;
    }

    source.service();
    if (failure == InjectedFailure::Busy
        || failure == InjectedFailure::Get
        || failure == InjectedFailure::Calibration) {
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

void test_ads1115_cached_readings_expire_and_unknown_ids_are_absent()
{
    using namespace std::chrono_literals;
    FakeAds1115Backend backend;
    FakeSampleConverter converter;
    FakeMonotonicClock clock;
    Ads1115FoodProbeSource source{
        valid_configuration(), backend, converter, clock,
    };

    complete_one_conversion(source, clock);
    complete_one_conversion(source, clock);
    assert(source.read(1U));
    assert(source.read(2U));
    assert(!source.read(99U));

    clock.advance(99ms);
    assert(source.read(1U));
    assert(source.read(2U));
    clock.advance(1ms);
    assert(!source.read(1U));
    assert(source.read(2U));
    clock.advance(1ms);
    assert(!source.read(2U));
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
    const auto start_allocations = allocation_probe::end();
    assert(start_allocations == 0U);

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
    smoker::app::SmokerApplication application{
        chamber,
        food_source,
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

    complete_one_conversion(food_source, acquisition_clock);
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
    test_ads1115_two_devices_and_channels_are_sequenced_without_reuse();
    test_ads1115_start_and_read_never_share_a_service_step();
    test_ads1115_busy_and_stuck_conversion_never_read_or_block();
    test_ads1115_mux_gain_rate_changes_require_a_new_completed_conversion();
    test_ads1115_per_probe_failures_clear_only_the_affected_sample();
    test_ads1115_cached_readings_expire_and_unknown_ids_are_absent();
    test_ads1115_steady_state_service_and_read_are_observed_allocation_free();
    test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control();
    return 0;
}

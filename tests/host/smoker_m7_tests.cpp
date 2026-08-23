#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/max31865_sensor.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>

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

using smoker::platform::Max31865InitializationStatus;
using smoker::platform::Max31865ReadResult;
using smoker::platform::Max31865ReadStatus;

class FakeMonotonicClock final : public smoker::app::IClock {
public:
    [[nodiscard]] smoker::core::MonotonicTimePoint now() const noexcept override
    {
        return now_;
    }

    void set(const smoker::core::Duration value) noexcept
    {
        now_ = smoker::core::MonotonicTimePoint{value};
    }

    void advance(const smoker::core::Duration value) noexcept
    {
        now_ += value;
    }

private:
    smoker::core::MonotonicTimePoint now_{};
};

class FakeMax31865Backend final : public smoker::platform::IMax31865Backend {
public:
    explicit FakeMax31865Backend(
        const Max31865InitializationStatus initialization,
        const std::span<const Max31865ReadResult> results = {}
    ) noexcept
        : initialization_{initialization}
        , results_{results}
    {
    }

    [[nodiscard]] Max31865InitializationStatus initialize() noexcept override
    {
        ++initialization_calls;
        return initialization_;
    }

    [[nodiscard]] Max31865ReadResult read_continuous() noexcept override
    {
        ++read_calls;
        if (next_result_ >= results_.size()) {
            return {Max31865ReadStatus::DriverError, 0.0F};
        }
        return results_[next_result_++];
    }

    std::size_t initialization_calls{0U};
    std::size_t read_calls{0U};

private:
    Max31865InitializationStatus initialization_;
    std::span<const Max31865ReadResult> results_;
    std::size_t next_result_{0U};
};

class FreshnessAwareFakeBackend final
    : public smoker::platform::IMax31865Backend {
public:
    FreshnessAwareFakeBackend(
        const smoker::platform::Max31865FilterFrequency filter,
        const smoker::app::IClock& clock
    ) noexcept
        : readiness_{filter, clock}
    {
    }

    [[nodiscard]] Max31865InitializationStatus initialize() noexcept override
    {
        ++initialization_calls;
        configured_ = true;
        readiness_.continuous_configuration_applied();
        return Max31865InitializationStatus::ConfiguredAwaitingFirstSample;
    }

    [[nodiscard]] Max31865ReadResult read_continuous() noexcept override
    {
        ++read_attempts;
        if (!configured_) {
            return {Max31865ReadStatus::DriverError, 0.0F};
        }
        if (!readiness_.sample_ready()) {
            return {Max31865ReadStatus::NotReady, 0.0F};
        }

        ++register_reads;
        const auto result = current_result_;
        if (result.status == Max31865ReadStatus::Fault) {
            apply_successful_reconfiguration();
        }
        return result;
    }

    void set_result(const Max31865ReadResult result) noexcept
    {
        current_result_ = result;
    }

    void apply_successful_reconfiguration() noexcept
    {
        ++reconfigurations;
        readiness_.continuous_configuration_applied();
    }

    std::size_t initialization_calls{0U};
    std::size_t read_attempts{0U};
    std::size_t register_reads{0U};
    std::size_t reconfigurations{0U};

private:
    smoker::platform::Max31865ReadinessPolicy readiness_;
    Max31865ReadResult current_result_{Max31865ReadStatus::DriverError, 0.0F};
    bool configured_{false};
};

[[nodiscard]] smoker::core::Temperature temperature(const float celsius)
{
    const auto value = smoker::core::Temperature::from_celsius(celsius);
    assert(value.has_value());
    return *value;
}

void test_max31865_configuration_policy_requires_explicit_valid_values()
{
    using smoker::platform::Max31865ConversionConfiguration;
    using smoker::platform::Max31865FilterFrequency;
    using smoker::platform::Max31865RtdStandard;
    using smoker::platform::valid_max31865_conversion_configuration;

    const Max31865ConversionConfiguration valid{
        123.0F,
        Max31865FilterFrequency::Hz50,
        Max31865RtdStandard::Its90,
    };
    assert(valid_max31865_conversion_configuration(valid));

    auto invalid = valid;
    invalid.reference_resistance_ohms = 0.0F;
    assert(!valid_max31865_conversion_configuration(invalid));
    invalid.reference_resistance_ohms =
        std::numeric_limits<float>::quiet_NaN();
    assert(!valid_max31865_conversion_configuration(invalid));
    invalid = valid;
    invalid.filter = static_cast<Max31865FilterFrequency>(255U);
    assert(!valid_max31865_conversion_configuration(invalid));
    invalid = valid;
    invalid.standard = static_cast<Max31865RtdStandard>(255U);
    assert(!valid_max31865_conversion_configuration(invalid));
}

void test_max31865_60_hz_first_conversion_boundary()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    clock.set(500ms);
    smoker::platform::Max31865ReadinessPolicy readiness{
        smoker::platform::Max31865FilterFrequency::Hz60,
        clock,
    };

    readiness.continuous_configuration_applied();
    assert(!readiness.sample_ready());
    clock.advance(54ms);
    assert(!readiness.sample_ready());
    clock.advance(1ms);
    assert(readiness.sample_ready());
    clock.advance(1ms);
    assert(readiness.sample_ready());
}

void test_max31865_50_hz_first_conversion_boundary()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    clock.set(900ms);
    smoker::platform::Max31865ReadinessPolicy readiness{
        smoker::platform::Max31865FilterFrequency::Hz50,
        clock,
    };

    readiness.continuous_configuration_applied();
    assert(!readiness.sample_ready());
    clock.advance(65ms);
    assert(!readiness.sample_ready());
    clock.advance(1ms);
    assert(readiness.sample_ready());
    clock.advance(1ms);
    assert(readiness.sample_ready());
}

void test_max31865_initialization_and_configuration_failures_are_absent()
{
    constexpr std::array failures{
        Max31865InitializationStatus::InvalidConfiguration,
        Max31865InitializationStatus::DescriptorError,
        Max31865InitializationStatus::DeviceConfigurationError,
    };
    for (const auto failure : failures) {
        FakeMax31865Backend backend{failure};
        smoker::platform::Max31865ChamberSensor sensor{backend};
        assert(!sensor.configured());
        assert(!sensor.read().has_value());
        assert(backend.initialization_calls == 1U);
        assert(backend.read_calls == 0U);
    }
}

void test_max31865_read_policy_never_reuses_a_previous_value()
{
    const std::array results{
        Max31865ReadResult{Max31865ReadStatus::Valid, 123.5F},
        Max31865ReadResult{Max31865ReadStatus::NotReady, 0.0F},
        Max31865ReadResult{
            Max31865ReadStatus::Valid,
            std::numeric_limits<float>::infinity(),
        },
        Max31865ReadResult{
            Max31865ReadStatus::Valid,
            std::numeric_limits<float>::quiet_NaN(),
        },
        Max31865ReadResult{Max31865ReadStatus::DriverError, 0.0F},
        Max31865ReadResult{Max31865ReadStatus::Fault, 0.0F},
        Max31865ReadResult{Max31865ReadStatus::Valid, 87.25F},
    };
    FakeMax31865Backend backend{
        Max31865InitializationStatus::ConfiguredAwaitingFirstSample,
        results,
    };
    smoker::platform::Max31865ChamberSensor sensor{backend};

    const auto first = sensor.read();
    assert(first && first->celsius() == 123.5F);
    assert(!sensor.read().has_value());
    assert(!sensor.read().has_value());
    assert(!sensor.read().has_value());
    assert(!sensor.read().has_value());
    assert(!sensor.read().has_value());
    const auto recovered = sensor.read();
    assert(recovered && recovered->celsius() == 87.25F);
}

void test_max31865_por_value_is_not_exposed_before_readiness()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz60,
        clock,
    };
    backend.set_result({Max31865ReadStatus::Valid, -242.02F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    assert(sensor.configured());
    assert(!sensor.read().has_value());
    clock.advance(54ms);
    assert(!sensor.read().has_value());
    assert(backend.register_reads == 0U);

    backend.set_result({Max31865ReadStatus::Valid, 73.5F});
    clock.advance(1ms);
    const auto fresh = sensor.read();
    assert(fresh && fresh->celsius() == 73.5F);
    assert(backend.register_reads == 1U);
}

void test_max31865_reconfiguration_resets_readiness_without_reuse()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz50,
        clock,
    };
    backend.set_result({Max31865ReadStatus::Valid, 121.0F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    clock.advance(66ms);
    const auto first = sensor.read();
    assert(first && first->celsius() == 121.0F);
    assert(backend.register_reads == 1U);

    backend.apply_successful_reconfiguration();
    assert(!sensor.read().has_value());
    assert(!sensor.read().has_value());
    clock.advance(65ms);
    assert(!sensor.read().has_value());
    assert(backend.register_reads == 1U);

    backend.set_result({Max31865ReadStatus::Valid, 84.0F});
    clock.advance(1ms);
    const auto fresh = sensor.read();
    assert(fresh && fresh->celsius() == 84.0F);
    assert(backend.register_reads == 2U);
}

void test_max31865_reinitialization_resets_readiness()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz60,
        clock,
    };
    backend.set_result({Max31865ReadStatus::Valid, 92.0F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    clock.advance(55ms);
    assert(sensor.read().has_value());
    assert(backend.register_reads == 1U);

    assert(
        backend.initialize()
        == Max31865InitializationStatus::ConfiguredAwaitingFirstSample
    );
    backend.set_result({Max31865ReadStatus::Valid, 31.0F});
    assert(!sensor.read().has_value());
    clock.advance(54ms);
    assert(!sensor.read().has_value());
    assert(backend.register_reads == 1U);

    backend.set_result({Max31865ReadStatus::Valid, 64.0F});
    clock.advance(1ms);
    const auto fresh = sensor.read();
    assert(fresh && fresh->celsius() == 64.0F);
    assert(backend.register_reads == 2U);
}

void test_max31865_fault_recovery_requires_fresh_current_value()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz60,
        clock,
    };
    backend.set_result({Max31865ReadStatus::Fault, 0.0F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    clock.advance(55ms);
    assert(!sensor.read().has_value());
    assert(backend.register_reads == 1U);
    assert(backend.reconfigurations == 1U);

    backend.set_result({Max31865ReadStatus::Valid, 119.0F});
    assert(!sensor.read().has_value());
    clock.advance(54ms);
    assert(!sensor.read().has_value());
    assert(backend.register_reads == 1U);

    backend.set_result({Max31865ReadStatus::Valid, 76.0F});
    clock.advance(1ms);
    const auto recovered = sensor.read();
    assert(recovered && recovered->celsius() == 76.0F);
    assert(backend.register_reads == 2U);
}

void test_max31865_read_is_observed_allocation_free()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz60,
        clock,
    };
    backend.set_result({Max31865ReadStatus::Valid, 42.0F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    allocation_probe::begin();
    const auto early = sensor.read();
    const auto early_allocations = allocation_probe::end();
    assert(!early.has_value());
    assert(early_allocations == 0U);

    clock.advance(55ms);
    allocation_probe::begin();
    const auto reading = sensor.read();
    const auto ready_allocations = allocation_probe::end();
    assert(reading && reading->celsius() == 42.0F);
    assert(ready_allocations == 0U);
    assert(backend.register_reads == 1U);
}

void test_max31865_premature_application_tick_latches_fault_and_heater_off()
{
    using namespace std::chrono_literals;
    FakeMonotonicClock readiness_clock;
    FreshnessAwareFakeBackend backend{
        smoker::platform::Max31865FilterFrequency::Hz60,
        readiness_clock,
    };
    backend.set_result({Max31865ReadStatus::Valid, 30.0F});
    smoker::platform::Max31865ChamberSensor sensor{backend};

    const std::array probes{
        smoker::core::FoodProbeConfig{
            1U,
            "M7 simulated food probe",
            smoker::core::ProbeRole::Meat,
            std::nullopt,
            true,
            false,
        },
    };
    smoker::platform::SimulatedFoodProbeSource food_source{probes};
    smoker::platform::DeterministicChamberController chamber_controller;
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    smoker::app::SmokerApplication application{
        sensor,
        food_source,
        chamber_controller,
        heater,
        clock,
        events,
        smoker::core::SafetyLimits{temperature(150.0F)},
        probes,
    };

    const smoker::core::Recipe recipe{
        7U,
        "M7 host recipe",
        smoker::core::Stage{7U, "M7 stage", temperature(110.0F), std::nullopt},
    };
    assert(application.submit(smoker::app::StartSessionCommand{7U, recipe}));
    application.tick();
    auto snapshot = application.snapshot();
    assert(snapshot.session_status == smoker::core::SessionStatus::Fault);
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ChamberSensorInvalid);
    assert(!snapshot.chamber_temperature.has_value());
    assert(heater.last_demand().percent() == 0.0F);
    assert(backend.register_reads == 0U);

    readiness_clock.advance(55ms);
    application.tick();
    snapshot = application.snapshot();
    assert(snapshot.chamber_temperature
        && snapshot.chamber_temperature->celsius() == 30.0F);
    assert(snapshot.session_status == smoker::core::SessionStatus::Fault);
    assert(snapshot.active_fault.has_value());
    assert(heater.last_demand().percent() == 0.0F);
}

} // namespace

int main()
{
    test_max31865_configuration_policy_requires_explicit_valid_values();
    test_max31865_60_hz_first_conversion_boundary();
    test_max31865_50_hz_first_conversion_boundary();
    test_max31865_initialization_and_configuration_failures_are_absent();
    test_max31865_read_policy_never_reuses_a_previous_value();
    test_max31865_por_value_is_not_exposed_before_readiness();
    test_max31865_reconfiguration_resets_readiness_without_reuse();
    test_max31865_reinitialization_resets_readiness();
    test_max31865_fault_recovery_requires_fresh_current_value();
    test_max31865_read_is_observed_allocation_free();
    test_max31865_premature_application_tick_latches_fault_and_heater_off();
    return 0;
}

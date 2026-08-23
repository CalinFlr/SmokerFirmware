#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/pid_chamber_controller.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

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

[[nodiscard]] smoker::core::Temperature temperature(const float celsius)
{
    const auto value = smoker::core::Temperature::from_celsius(celsius);
    assert(value);
    return *value;
}

[[nodiscard]] smoker::core::HeaterDemand demand(const float percent)
{
    const auto value = smoker::core::HeaterDemand::from_percent(percent);
    assert(value);
    return *value;
}

// These coefficients are test fixtures for API behavior only. They are not
// smoker gains, tuning results, or production recommendations.
[[nodiscard]] smoker::platform::PidControllerConfiguration
test_fixture_positional_pid_configuration() noexcept
{
    return smoker::platform::PidControllerConfiguration{
        .proportional_gain = 2.0F,
        .integral_gain = 0.5F,
        .derivative_gain = 0.25F,
        .minimum_output_percent = 0.0F,
        .maximum_output_percent = 100.0F,
        .calculation_form = smoker::platform::PidCalculationForm::Positional,
        .positional_accumulated_error_bounds =
            smoker::platform::PositionalAccumulatedErrorBounds{-50.0F, 50.0F},
    };
}

[[nodiscard]] smoker::platform::PidControllerConfiguration
test_fixture_incremental_pid_configuration() noexcept
{
    auto configuration = test_fixture_positional_pid_configuration();
    configuration.calculation_form =
        smoker::platform::PidCalculationForm::Incremental;
    configuration.positional_accumulated_error_bounds.reset();
    return configuration;
}

class FakePidBackend final : public smoker::platform::IPidControllerBackend {
public:
    bool initialize_success{true};
    bool compute_success{true};
    bool reset_success{true};
    float output_percent{0.0F};
    float last_error{0.0F};
    std::size_t initialize_calls{0U};
    std::size_t compute_calls{0U};
    std::size_t reset_calls{0U};

    [[nodiscard]] bool initialize(
        const smoker::platform::PidControllerConfiguration& configuration
    ) noexcept override
    {
        ++initialize_calls;
        observed_configuration = configuration;
        return initialize_success;
    }

    [[nodiscard]] bool compute(
        const float error, float& result_percent
    ) noexcept override
    {
        ++compute_calls;
        last_error = error;
        result_percent = output_percent;
        return compute_success;
    }

    [[nodiscard]] bool reset() noexcept override
    {
        ++reset_calls;
        return reset_success;
    }

    smoker::platform::PidControllerConfiguration observed_configuration{
        test_fixture_positional_pid_configuration()
    };
};

void expect_invalid_configuration(
    const smoker::platform::PidControllerConfiguration configuration
)
{
    FakePidBackend backend;
    smoker::platform::PidChamberController controller{configuration, backend};
    assert(!controller.initialized());
    assert(backend.initialize_calls == 0U);
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    assert(!controller.reset());
}

void test_valid_positional_configuration_with_accumulated_error_bounds()
{
    FakePidBackend backend;
    smoker::platform::PidChamberController controller{
        test_fixture_positional_pid_configuration(), backend,
    };
    assert(controller.initialized());
    assert(backend.initialize_calls == 1U);
    const auto bounds =
        backend.observed_configuration.positional_accumulated_error_bounds;
    assert(bounds && bounds->minimum == -50.0F && bounds->maximum == 50.0F);
}

void test_invalid_common_and_positional_configurations()
{
    auto configuration = test_fixture_positional_pid_configuration();
    configuration.proportional_gain = std::numeric_limits<float>::infinity();
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.integral_gain = std::numeric_limits<float>::quiet_NaN();
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.derivative_gain = -0.1F;
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.minimum_output_percent = -1.0F;
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.maximum_output_percent = 101.0F;
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.minimum_output_percent = 60.0F;
    configuration.maximum_output_percent = 40.0F;
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds.reset();
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{1.0F, 2.0F};
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{-2.0F, -1.0F};
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{2.0F, -2.0F};
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{
            -1.0F, std::numeric_limits<float>::infinity(),
        };
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{
            std::numeric_limits<float>::quiet_NaN(), 1.0F,
        };
    expect_invalid_configuration(configuration);
    configuration = test_fixture_positional_pid_configuration();
    configuration.calculation_form =
        static_cast<smoker::platform::PidCalculationForm>(99U);
    expect_invalid_configuration(configuration);
}

void test_valid_incremental_configuration_has_no_integral_bound_promise()
{
    FakePidBackend backend;
    smoker::platform::PidChamberController controller{
        test_fixture_incremental_pid_configuration(), backend,
    };
    assert(controller.initialized());
    assert(backend.initialize_calls == 1U);
    assert(backend.observed_configuration.calculation_form
        == smoker::platform::PidCalculationForm::Incremental);
    assert(!backend.observed_configuration.positional_accumulated_error_bounds);
}

void test_incremental_configuration_rejects_contradictory_positional_bounds()
{
    auto configuration = test_fixture_incremental_pid_configuration();
    configuration.positional_accumulated_error_bounds =
        smoker::platform::PositionalAccumulatedErrorBounds{-10.0F, 10.0F};
    expect_invalid_configuration(configuration);
}

void test_backend_initialization_failure()
{
    FakePidBackend backend;
    backend.initialize_success = false;
    smoker::platform::PidChamberController controller{
        test_fixture_positional_pid_configuration(), backend,
    };
    assert(!controller.initialized());
    assert(backend.initialize_calls == 1U);
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    assert(!controller.reset());
}

void test_target_minus_measurement_and_normalized_output()
{
    FakePidBackend backend;
    backend.output_percent = 42.5F;
    smoker::platform::PidChamberController controller{
        test_fixture_positional_pid_configuration(), backend,
    };
    const auto result = controller.request(temperature(85.0F), temperature(100.0F));
    assert(result && result->percent() == 42.5F);
    assert(backend.last_error == 15.0F);
    assert(backend.observed_configuration.calculation_form
        == smoker::platform::PidCalculationForm::Positional);
}

void test_backend_compute_and_output_failures()
{
    FakePidBackend backend;
    smoker::platform::PidChamberController controller{
        test_fixture_positional_pid_configuration(), backend,
    };

    backend.compute_success = false;
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    backend.compute_success = true;
    backend.output_percent = std::numeric_limits<float>::quiet_NaN();
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    backend.output_percent = std::numeric_limits<float>::infinity();
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    backend.output_percent = -0.1F;
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));
    backend.output_percent = 100.1F;
    assert(!controller.request(temperature(20.0F), temperature(100.0F)));

    const auto maximum = temperature(std::numeric_limits<float>::max());
    const auto negative_maximum = temperature(-std::numeric_limits<float>::max());
    const auto compute_calls = backend.compute_calls;
    assert(!controller.request(negative_maximum, maximum));
    assert(backend.compute_calls == compute_calls);
}

void test_reset_failure_and_steady_allocation_without_destructor_reset()
{
    FakePidBackend backend;
    backend.output_percent = 25.0F;
    {
        smoker::platform::PidChamberController controller{
            test_fixture_positional_pid_configuration(), backend,
        };
        allocation_probe::begin();
        const auto result = controller.request(
            temperature(20.0F), temperature(100.0F)
        );
        const bool reset_ok = controller.reset();
        const auto allocations = allocation_probe::end();
        assert(result && result->percent() == 25.0F);
        assert(reset_ok);
        assert(allocations == 0U);

        backend.reset_success = false;
        assert(!controller.reset());
        backend.reset_success = true;
    }
    assert(backend.reset_calls == 2U);
}

class RecordingController final : public smoker::app::IChamberController {
public:
    std::optional<smoker::core::HeaterDemand> next_request{demand(70.0F)};
    bool reset_success{true};
    std::size_t request_calls{0U};
    std::size_t reset_calls{0U};
    std::optional<smoker::core::Temperature> last_measurement;
    std::optional<smoker::core::Temperature> last_target;

    [[nodiscard]] std::optional<smoker::core::HeaterDemand> request(
        const smoker::core::Temperature chamber_temperature,
        const smoker::core::Temperature chamber_target
    ) noexcept override
    {
        ++request_calls;
        last_measurement = chamber_temperature;
        last_target = chamber_target;
        return next_request;
    }

    [[nodiscard]] bool reset() noexcept override
    {
        ++reset_calls;
        return reset_success;
    }
};

enum class ConstructionAction : std::uint8_t {
    HeaterWrite,
    ControllerReset,
};

class ConstructionOrderRecorder final {
public:
    void record(const ConstructionAction action) noexcept
    {
        assert(count < actions.size());
        actions[count++] = action;
    }

    std::array<ConstructionAction, 4U> actions{};
    std::size_t count{0U};
};

class ConstructionOrderController final : public smoker::app::IChamberController {
public:
    explicit ConstructionOrderController(ConstructionOrderRecorder& recorder)
        : recorder_{recorder}
    {
    }

    [[nodiscard]] std::optional<smoker::core::HeaterDemand> request(
        smoker::core::Temperature, smoker::core::Temperature
    ) noexcept override
    {
        return std::nullopt;
    }

    [[nodiscard]] bool reset() noexcept override
    {
        recorder_.record(ConstructionAction::ControllerReset);
        return true;
    }

private:
    ConstructionOrderRecorder& recorder_;
};

class ConstructionOrderHeater final : public smoker::app::IHeaterOutput {
public:
    explicit ConstructionOrderHeater(ConstructionOrderRecorder& recorder)
        : recorder_{recorder}
    {
    }

    void write(const smoker::core::HeaterDemand demand_value) noexcept override
    {
        recorder_.record(ConstructionAction::HeaterWrite);
        last_demand = demand_value;
    }

    smoker::core::HeaterDemand last_demand{smoker::core::HeaterDemand::off()};

private:
    ConstructionOrderRecorder& recorder_;
};

class ApplicationFixture final {
public:
    explicit ApplicationFixture(RecordingController& controller)
        : food_source{probes}
        , application{
              chamber,
              food_source,
              controller,
              heater,
              clock,
              events,
              smoker::core::SafetyLimits{temperature(150.0F)},
              probes,
          }
    {
    }

    [[nodiscard]] bool start(
        const std::optional<smoker::core::Temperature> target,
        const smoker::core::SessionId session_id = 1U
    )
    {
        return application.submit(smoker::app::StartSessionCommand{
            session_id,
            smoker::core::Recipe{
                8U,
                "M8 application fixture",
                smoker::core::Stage{8U, "M8 stage", target, std::nullopt},
            },
        });
    }

    std::array<smoker::core::FoodProbeConfig, 1U> probes{
        smoker::core::FoodProbeConfig{
            1U,
            "M8 simulated food probe",
            smoker::core::ProbeRole::Meat,
            std::nullopt,
            true,
            false,
        },
    };
    smoker::platform::SimulatedChamberSensor chamber{temperature(25.0F)};
    smoker::platform::SimulatedFoodProbeSource food_source;
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    smoker::app::SmokerApplication application;
};

void test_constructor_writes_observable_off_before_first_controller_reset()
{
    std::array<smoker::core::FoodProbeConfig, 1U> probes{
        smoker::core::FoodProbeConfig{
            1U,
            "M8 construction-order probe",
            smoker::core::ProbeRole::Meat,
            std::nullopt,
            true,
            false,
        },
    };
    ConstructionOrderRecorder recorder;
    ConstructionOrderController controller{recorder};
    ConstructionOrderHeater heater{recorder};
    smoker::platform::SimulatedChamberSensor chamber{temperature(25.0F)};
    smoker::platform::SimulatedFoodProbeSource food_source{probes};
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;

    smoker::app::SmokerApplication application{
        chamber,
        food_source,
        controller,
        heater,
        clock,
        events,
        smoker::core::SafetyLimits{temperature(150.0F)},
        probes,
    };

    assert(recorder.count == 2U);
    assert(recorder.actions[0] == ConstructionAction::HeaterWrite);
    assert(recorder.actions[1] == ConstructionAction::ControllerReset);
    assert(heater.last_demand.percent() == 0.0F);
    assert(application.snapshot().heater_demand.percent() == 0.0F);
}

void test_application_off_reset_and_stop_lifecycle()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    assert(controller.reset_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    fixture.application.tick();
    assert(controller.request_calls == 0U && controller.reset_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    assert(fixture.start(std::nullopt));
    fixture.application.tick();
    assert(controller.request_calls == 0U && controller.reset_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    assert(fixture.application.submit(
        smoker::app::SetChamberTargetCommand{temperature(110.0F)}
    ));
    fixture.application.tick();
    assert(controller.request_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 70.0F);

    assert(fixture.application.submit(
        smoker::app::SetChamberTargetCommand{std::nullopt}
    ));
    fixture.application.tick();
    assert(controller.reset_calls == 2U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    assert(fixture.application.submit(
        smoker::app::SetChamberTargetCommand{temperature(110.0F)}
    ));
    fixture.application.tick();
    assert(controller.request_calls == 2U);
    assert(fixture.application.submit(smoker::app::StopSessionCommand{}));
    fixture.application.tick();
    assert(controller.reset_calls == 3U);
    assert(fixture.heater.last_demand().percent() == 0.0F);
    fixture.application.tick();
    assert(controller.request_calls == 2U);
    assert(fixture.heater.last_demand().percent() == 0.0F);
}

void test_same_tick_target_removal_and_restoration_uses_final_state()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    assert(fixture.start(temperature(110.0F)));
    fixture.application.tick();
    assert(controller.request_calls == 1U);
    assert(controller.reset_calls == 1U);

    assert(fixture.application.submit(
        smoker::app::SetChamberTargetCommand{std::nullopt}
    ));
    assert(fixture.application.submit(
        smoker::app::SetChamberTargetCommand{temperature(120.0F)}
    ));
    fixture.application.tick();

    assert(controller.request_calls == 2U);
    assert(controller.reset_calls == 1U);
    assert(controller.last_target == temperature(120.0F));
    assert(fixture.heater.last_demand().percent() == 70.0F);
}

void test_boot_reset_failure_latches_before_any_request()
{
    RecordingController controller;
    controller.reset_success = false;
    ApplicationFixture fixture{controller};
    controller.reset_success = true;
    assert(fixture.start(temperature(110.0F)));
    fixture.application.tick();
    const auto snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ControlLoopFailure);
    assert(snapshot.heater_demand.percent() == 0.0F);
    assert(controller.request_calls == 0U);
    assert(controller.reset_calls == 2U);
}

void test_invalid_measurement_fault_resets_and_never_resumes()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    assert(fixture.start(temperature(110.0F)));
    fixture.application.tick();
    assert(fixture.heater.last_demand().percent() == 70.0F);

    fixture.chamber.set_reading(std::nullopt);
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ChamberSensorInvalid);
    assert(controller.reset_calls == 2U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    fixture.chamber.set_reading(temperature(25.0F));
    fixture.application.tick();
    assert(controller.request_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 0.0F);
}

void test_compute_failure_latches_and_requires_clear_then_start()
{
    RecordingController controller;
    controller.next_request.reset();
    ApplicationFixture fixture{controller};
    assert(fixture.start(temperature(110.0F), 1U));
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ControlLoopFailure);
    assert(snapshot.heater_demand.percent() == 0.0F);
    assert(controller.reset_calls == 2U);

    controller.next_request = demand(65.0F);
    fixture.application.tick();
    assert(controller.request_calls == 1U);
    assert(fixture.heater.last_demand().percent() == 0.0F);

    assert(fixture.application.submit(smoker::app::ClearResolvedFaultCommand{}));
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    assert(!snapshot.active_fault);
    assert(snapshot.session_status == smoker::core::SessionStatus::Stopped);
    assert(snapshot.heater_demand.percent() == 0.0F);

    assert(fixture.start(temperature(110.0F), 2U));
    fixture.application.tick();
    assert(fixture.application.snapshot().session_status
        == smoker::core::SessionStatus::Running);
    assert(fixture.heater.last_demand().percent() == 65.0F);
}

void test_reset_failure_fails_closed_and_can_only_resolve_latched_fault()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    assert(fixture.start(temperature(110.0F)));
    fixture.application.tick();
    controller.reset_success = false;
    assert(fixture.application.submit(smoker::app::StopSessionCommand{}));
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ControlLoopFailure);
    assert(snapshot.heater_demand.percent() == 0.0F);

    controller.reset_success = true;
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault);
    assert(snapshot.heater_demand.percent() == 0.0F);
    assert(controller.request_calls == 1U);
    assert(controller.reset_calls == 3U);

    assert(fixture.application.submit(smoker::app::ClearResolvedFaultCommand{}));
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    assert(!snapshot.active_fault);
    assert(snapshot.session_status == smoker::core::SessionStatus::Stopped);
    assert(snapshot.heater_demand.percent() == 0.0F);
    assert(controller.request_calls == 1U);
}

void test_reset_failure_masked_by_safety_fault_remains_fail_closed()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    fixture.chamber.set_reading(temperature(151.0F));
    assert(fixture.start(temperature(140.0F)));
    controller.reset_success = false;
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ChamberOverTemperature);
    assert(snapshot.heater_demand.percent() == 0.0F);

    fixture.chamber.set_reading(temperature(25.0F));
    controller.reset_success = true;
    fixture.application.tick();
    assert(fixture.application.submit(smoker::app::ClearResolvedFaultCommand{}));
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ControlLoopFailure);
    assert(snapshot.heater_demand.percent() == 0.0F);
    assert(controller.request_calls == 1U);
}

void test_safety_overrides_positive_request_before_only_write()
{
    RecordingController controller;
    controller.next_request = demand(90.0F);
    ApplicationFixture fixture{controller};
    fixture.chamber.set_reading(temperature(151.0F));
    assert(fixture.start(temperature(140.0F)));
    fixture.application.tick();
    const auto snapshot = fixture.application.snapshot();
    assert(controller.request_calls == 1U);
    assert(controller.last_measurement == temperature(151.0F));
    assert(snapshot.active_fault
        && snapshot.active_fault->code
            == smoker::core::FaultCode::ChamberOverTemperature);
    assert(controller.reset_calls == 2U);
    assert(fixture.heater.last_demand().percent() == 0.0F);
}

void test_firmware_update_interlock_never_calls_controller()
{
    RecordingController controller;
    ApplicationFixture fixture{controller};
    assert(fixture.application.submit(smoker::app::PrepareFirmwareUpdateCommand{}));
    fixture.application.tick();
    assert(fixture.application.snapshot().firmware_update_active);
    assert(fixture.start(temperature(110.0F)));
    fixture.application.tick();
    assert(controller.request_calls == 0U);
    assert(fixture.heater.last_demand().percent() == 0.0F);
}

void test_deterministic_production_adapter_preserves_m2_behavior()
{
    smoker::platform::DeterministicChamberController controller;
    const auto heating = controller.request(temperature(25.0F), temperature(110.0F));
    const auto off = controller.request(temperature(110.0F), temperature(110.0F));
    assert(heating && heating->percent() == 100.0F);
    assert(off && off->percent() == 0.0F);
    assert(controller.reset());
}

} // namespace

int main()
{
    test_valid_positional_configuration_with_accumulated_error_bounds();
    test_invalid_common_and_positional_configurations();
    test_valid_incremental_configuration_has_no_integral_bound_promise();
    test_incremental_configuration_rejects_contradictory_positional_bounds();
    test_backend_initialization_failure();
    test_target_minus_measurement_and_normalized_output();
    test_backend_compute_and_output_failures();
    test_reset_failure_and_steady_allocation_without_destructor_reset();
    test_constructor_writes_observable_off_before_first_controller_reset();
    test_application_off_reset_and_stop_lifecycle();
    test_same_tick_target_removal_and_restoration_uses_final_state();
    test_boot_reset_failure_latches_before_any_request();
    test_invalid_measurement_fault_resets_and_never_resumes();
    test_compute_failure_latches_and_requires_clear_then_start();
    test_reset_failure_fails_closed_and_can_only_resolve_latched_fault();
    test_reset_failure_masked_by_safety_fault_remains_fail_closed();
    test_safety_overrides_positive_request_before_only_write();
    test_firmware_update_interlock_never_calls_controller();
    test_deterministic_production_adapter_preserves_m2_behavior();
    return 0;
}

#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace allocation_probe {

struct Counts final {
    std::size_t allocations{};
    std::size_t deallocations{};
};

bool enabled = false;
Counts counts;

void begin() noexcept
{
    counts = {};
    enabled = true;
}

[[nodiscard]] Counts end() noexcept
{
    enabled = false;
    return counts;
}

} // namespace allocation_probe

void* operator new(const std::size_t size)
{
    if (allocation_probe::enabled) {
        ++allocation_probe::counts.allocations;
    }
    if (auto* memory = std::malloc(size)) {
        return memory;
    }
    std::abort();
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* const memory) noexcept
{
    if (allocation_probe::enabled && memory != nullptr) {
        ++allocation_probe::counts.deallocations;
    }
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

using namespace std::chrono_literals;
using smoker::app::AcknowledgeAlarmCommand;
using smoker::app::ClearResolvedFaultCommand;
using smoker::app::SetChamberTargetCommand;
using smoker::app::SetProbeAlarmEnabledCommand;
using smoker::app::SetProbeEnabledCommand;
using smoker::app::SetProbeTargetCommand;
using smoker::app::SmokerApplication;
using smoker::app::StartSessionCommand;
using smoker::app::StopSessionCommand;
using smoker::core::AlarmCode;
using smoker::core::EventType;
using smoker::core::FaultCode;
using smoker::core::FoodProbeConfig;
using smoker::core::ProbeRole;
using smoker::core::Recipe;
using smoker::core::SessionStatus;
using smoker::core::Stage;
using smoker::core::StageTimer;
using smoker::core::StopReason;
using smoker::core::Temperature;
using smoker::core::TimerCompletionAction;
using smoker::core::TimerStartCondition;
using smoker::core::TimerStartConditionType;

class TestContext final {
public:
    void expect(const bool condition, const std::string_view description)
    {
        if (condition) {
            return;
        }

        ++failure_count_;
        std::cerr << "FAIL: " << description << '\n';
    }

    [[nodiscard]] int failure_count() const noexcept
    {
        return failure_count_;
    }

private:
    int failure_count_{0};
};

[[nodiscard]] Temperature temperature(const float celsius)
{
    const auto result = Temperature::from_celsius(celsius);
    return *result;
}

[[nodiscard]] smoker::core::MonotonicTimePoint monotonic_time(const std::size_t milliseconds)
{
    return smoker::core::MonotonicTimePoint{
        smoker::core::Duration{static_cast<smoker::core::Duration::rep>(milliseconds)}
    };
}

[[nodiscard]] std::array<FoodProbeConfig, 2U> probe_configuration()
{
    return {
        FoodProbeConfig{1U, "Brisket", ProbeRole::Meat, temperature(70.0F), true, true},
        FoodProbeConfig{2U, "Ambient", ProbeRole::AmbientMonitor, std::nullopt, true, true},
    };
}

[[nodiscard]] StageTimer timer(
    const std::chrono::milliseconds duration,
    const TimerStartCondition condition,
    const TimerCompletionAction action = TimerCompletionAction::Notify
)
{
    return StageTimer{duration, condition, action};
}

[[nodiscard]] TimerStartCondition immediate()
{
    return TimerStartCondition{TimerStartConditionType::Immediate, std::nullopt, std::nullopt};
}

[[nodiscard]] TimerStartCondition chamber_at(const float celsius)
{
    return TimerStartCondition{
        TimerStartConditionType::ChamberTemperatureAtLeast,
        temperature(celsius),
        std::nullopt,
    };
}

[[nodiscard]] TimerStartCondition probe_at(
    const smoker::core::ProbeId probe_id, const float celsius
)
{
    return TimerStartCondition{
        TimerStartConditionType::ProbeTemperatureAtLeast,
        temperature(celsius),
        probe_id,
    };
}

[[nodiscard]] Recipe recipe(
    const float chamber_target,
    std::optional<StageTimer> stage_timer = std::nullopt,
    std::string_view name = "Host recipe"
)
{
    return Recipe{
        11U,
        std::string{name},
        Stage{21U, "Only stage", temperature(chamber_target), stage_timer},
    };
}

[[nodiscard]] bool has_event(
    const std::span<const smoker::core::Event> events, const EventType type
)
{
    return std::any_of(
        events.begin(), events.end(),
        [type](const smoker::core::Event& event) { return event.type == type; }
    );
}

[[nodiscard]] const smoker::core::Alarm* find_alarm(
    const smoker::app::SmokerSnapshot& snapshot,
    const AlarmCode code,
    const std::optional<smoker::core::ProbeId> probe_id
)
{
    const auto alarm = std::find_if(
        snapshot.active_alarms.begin(), snapshot.active_alarms.end(),
        [code, probe_id](const smoker::core::Alarm& value) {
            return value.code == code && value.probe_id == probe_id;
        }
    );
    return alarm == snapshot.active_alarms.end() ? nullptr : &*alarm;
}

class Fixture final {
public:
    Fixture()
        : probes{probe_configuration()}
        , chamber{temperature(20.0F)}
        , food_source{probes}
        , chamber_controller{}
        , application{
              chamber,
              food_source,
              chamber_controller,
              heater,
              clock,
              events,
              smoker::core::SafetyLimits{temperature(150.0F)},
              probes,
          }
    {
        static_cast<void>(food_source.set_reading(1U, temperature(20.0F)));
        static_cast<void>(food_source.set_reading(2U, temperature(20.0F)));
    }

    [[nodiscard]] bool start(const Recipe& selected_recipe, const smoker::core::SessionId id = 1U)
    {
        return application.submit(StartSessionCommand{id, selected_recipe});
    }

    std::array<FoodProbeConfig, 2U> probes;
    smoker::platform::SimulatedChamberSensor chamber;
    smoker::platform::SimulatedFoodProbeSource food_source;
    smoker::platform::DeterministicChamberController chamber_controller;
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    SmokerApplication application;
};

void test_m2(TestContext& context)
{
    Fixture fixture;
    context.expect(
        fixture.heater.last_demand().percent() == 0.0F,
        "M2: boot writes an observable OFF demand"
    );
    context.expect(fixture.start(recipe(100.0F)), "M2: start command fits the command queue");

    fixture.application.tick();
    context.expect(
        fixture.heater.last_demand().percent() == 100.0F,
        "M2: chamber below target produces full normalized demand"
    );

    fixture.chamber.set_reading(temperature(110.0F));
    fixture.application.tick();
    context.expect(
        fixture.heater.last_demand().percent() == 0.0F,
        "M2: chamber at or above target produces OFF demand"
    );

    fixture.chamber.set_reading(temperature(20.0F));
    fixture.application.tick();
    context.expect(
        fixture.heater.last_demand().percent() == 100.0F,
        "M2: identical simulated input produces the same output"
    );
    context.expect(
        fixture.heater.history().size() == 4U,
        "M2: simulated heater records boot and every control-cycle output"
    );

    Fixture monitoring_fixture;
    auto monitoring_recipe = recipe(100.0F);
    monitoring_recipe.stage.chamber_target.reset();
    context.expect(
        monitoring_fixture.start(monitoring_recipe),
        "M2: monitoring-only recipe is queued"
    );
    monitoring_fixture.application.tick();
    context.expect(
        monitoring_fixture.application.snapshot().chamber_temperature.has_value()
            && monitoring_fixture.heater.last_demand().percent() == 0.0F,
        "M2: absent chamber target still measures chamber and forces heater OFF"
    );
}

void test_m3_session_and_snapshot(TestContext& context)
{
    Fixture fixture;
    auto selected_recipe = recipe(100.0F, timer(10s, immediate()), "Original recipe");
    context.expect(fixture.start(selected_recipe, 41U), "M3: start command is queued");
    selected_recipe.name = "Edited saved recipe";
    selected_recipe.stage.chamber_target = temperature(120.0F);
    fixture.application.tick();

    auto snapshot = fixture.application.snapshot();
    context.expect(snapshot.session_status == SessionStatus::Running, "M3: Start enters RUNNING");
    context.expect(snapshot.session_id == 41U, "M3: session id is retained");
    context.expect(
        snapshot.recipe_snapshot && snapshot.recipe_snapshot->name == "Original recipe",
        "M3: Start snapshots the recipe"
    );
    context.expect(
        snapshot.chamber_target && snapshot.chamber_target->celsius() == 100.0F,
        "M3: editing the caller's recipe does not mutate the active target"
    );
    context.expect(snapshot.timer.started, "M3: immediate timer starts on the first cycle");

    context.expect(
        fixture.application.submit(SetChamberTargetCommand{temperature(90.0F)}),
        "M3: valid live chamber-target command is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().chamber_target
            && fixture.application.snapshot().chamber_target->celsius() == 90.0F
            && fixture.application.snapshot().recipe_snapshot->stage.chamber_target->celsius()
                == 100.0F,
        "M3: live target changes active state without modifying the recipe snapshot"
    );

    context.expect(
        fixture.start(recipe(90.0F), 42U),
        "M3: second start command can be submitted for semantic rejection"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_id == 41U,
        "M3: only one session can be active"
    );

    fixture.clock.advance(6s);
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(snapshot.timer.elapsed == 6s, "M3: timer uses injected monotonic time");

    context.expect(
        fixture.application.submit(StopSessionCommand{}),
        "M3: Stop command is queued"
    );
    fixture.clock.advance(1s);
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(snapshot.session_status == SessionStatus::Stopped, "M3: Stop is terminal");
    context.expect(snapshot.stop_reason == StopReason::User, "M3: manual reason is recorded");
    context.expect(snapshot.timer.elapsed == 7s, "M3: Stop freezes elapsed timer time");
    context.expect(snapshot.heater_demand.percent() == 0.0F, "M3: Stop forces heater OFF");
}

void test_m3_timer_conditions(TestContext& context)
{
    {
        Fixture fixture;
        fixture.chamber.set_reading(temperature(40.0F));
        context.expect(
            fixture.start(recipe(100.0F, timer(10s, chamber_at(50.0F)))),
            "M3: chamber-threshold recipe is queued"
        );
        fixture.application.tick();
        context.expect(
            !fixture.application.snapshot().timer.started,
            "M3: chamber-threshold timer waits below threshold"
        );

        fixture.chamber.set_reading(temperature(50.0F));
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().timer.started,
            "M3: chamber-threshold timer starts at threshold"
        );

        fixture.chamber.set_reading(temperature(30.0F));
        fixture.clock.advance(5s);
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().timer.elapsed == 5s,
            "M3: temperature falling later does not pause the timer"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F, timer(2s, probe_at(1U, 70.0F)))),
            "M3: probe-threshold recipe is queued"
        );
        fixture.application.tick();
        context.expect(
            !fixture.application.snapshot().timer.started,
            "M3: selected-probe timer waits below threshold"
        );

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(70.0F)));
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().timer.started,
            "M3: selected-probe timer starts at threshold"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F, timer(2s, immediate(), TimerCompletionAction::StopSession))),
            "M3: stop-on-completion recipe is queued"
        );
        fixture.application.tick();
        fixture.clock.advance(2s);
        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(snapshot.timer.completed, "M3: timer reaches completed state");
        context.expect(
            snapshot.session_status == SessionStatus::Stopped,
            "M3: STOP_SESSION completion stops the session"
        );
        context.expect(
            snapshot.stop_reason == StopReason::TimerCompleted,
            "M3: timer completion reason is recorded"
        );
        context.expect(
            snapshot.heater_demand.percent() == 0.0F,
            "M3: timer STOP_SESSION turns heater OFF in the same cycle"
        );
    }
}

void test_m3_probe_timer_availability(TestContext& context)
{
    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F, timer(2s, probe_at(1U, 70.0F)))),
            "M3: disconnect-aware probe timer is queued"
        );
        fixture.application.tick();

        static_cast<void>(fixture.food_source.set_reading(1U, std::nullopt));
        fixture.application.tick();
        context.expect(
            !fixture.application.snapshot().timer.started,
            "M3: disconnected selected probe leaves its timer waiting"
        );

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().timer.started
                && has_event(fixture.events.events(), EventType::ProbeReconnected),
            "M3: reconnected selected probe can start the waiting timer"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F, timer(2s, probe_at(1U, 70.0F)))),
            "M3: disable-aware probe timer is queued"
        );
        fixture.application.tick();
        context.expect(
            fixture.application.submit(SetProbeEnabledCommand{1U, false}),
            "M3: selected timer probe disable is queued"
        );
        fixture.application.tick();

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        context.expect(
            fixture.application.submit(SetProbeEnabledCommand{1U, true}),
            "M3: selected timer probe re-enable is queued"
        );
        fixture.application.tick();
        context.expect(
            !fixture.application.snapshot().timer.started,
            "M3: re-enable cycle waits because raw acquisition preceded the command"
        );

        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().timer.started,
            "M3: re-enabled selected probe can start the timer on its next valid sample"
        );
    }
}

void test_m4_invalid_and_latched_fault(TestContext& context)
{
    Fixture fixture;
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Idle,
        "M4: valid idle cycle remains IDLE"
    );
    context.expect(
        fixture.heater.last_demand().percent() == 0.0F,
        "M4: heater stays OFF outside RUNNING"
    );

    context.expect(fixture.start(recipe(100.0F)), "M4: session is queued");
    fixture.application.tick();
    context.expect(fixture.heater.last_demand().percent() == 100.0F, "M4: valid run can heat");

    fixture.chamber.set_reading(std::nullopt);
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    context.expect(snapshot.session_status == SessionStatus::Fault, "M4: invalid chamber faults");
    context.expect(
        snapshot.active_fault
            && snapshot.active_fault->code == FaultCode::ChamberSensorInvalid
            && snapshot.active_fault->latched,
        "M4: invalid chamber fault is latched"
    );
    context.expect(snapshot.heater_demand.percent() == 0.0F, "M4: fault overrides heater demand");

    context.expect(
        fixture.application.submit(ClearResolvedFaultCommand{}),
        "M4: unresolved clear command is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().active_fault.has_value(),
        "M4: active invalid condition cannot be cleared"
    );

    fixture.chamber.set_reading(temperature(20.0F));
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().active_fault.has_value(),
        "M4: signal recovery does not auto-clear a fault"
    );
    context.expect(
        fixture.heater.last_demand().percent() == 0.0F,
        "M4: signal recovery does not auto-resume heating"
    );

    context.expect(
        fixture.application.submit(ClearResolvedFaultCommand{}),
        "M4: resolved clear command is queued"
    );
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(!snapshot.active_fault, "M4: resolved fault can be acknowledged and cleared");
    context.expect(
        snapshot.session_status == SessionStatus::Stopped,
        "M4: clearing fault leaves session STOPPED"
    );
    context.expect(snapshot.heater_demand.percent() == 0.0F, "M4: clear does not restart heating");

    context.expect(fixture.start(recipe(100.0F), 2U), "M4: explicit restart is queued");
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Running
            && fixture.heater.last_demand().percent() == 100.0F,
        "M4: heating resumes only after a new explicit Start"
    );
}

void test_m4_over_temperature_and_limits(TestContext& context)
{
    Fixture fixture;
    context.expect(fixture.start(recipe(100.0F)), "M4: over-temperature session is queued");
    fixture.application.tick();

    fixture.chamber.set_reading(temperature(150.0F));
    fixture.application.tick();
    context.expect(
        !fixture.application.snapshot().active_fault
            && fixture.application.snapshot().heater_demand.percent() == 0.0F,
        "SF-003: the configured maximum boundary is valid and demand remains OFF above target"
    );

    fixture.chamber.set_reading(temperature(151.0F));
    fixture.application.tick();
    const auto faulted = fixture.application.snapshot();
    context.expect(
        faulted.active_fault
            && faulted.active_fault->code == FaultCode::ChamberOverTemperature,
        "M4: measured temperature above independent maximum faults"
    );
    context.expect(
        faulted.heater_demand.percent() == 0.0F,
        "M4: over-temperature forces heater OFF"
    );

    Fixture invalid_target_fixture;
    context.expect(
        invalid_target_fixture.start(recipe(151.0F)),
        "M4: unsafe recipe reaches application validation"
    );
    invalid_target_fixture.application.tick();
    context.expect(
        invalid_target_fixture.application.snapshot().session_status == SessionStatus::Idle,
        "M4: recipe target above configured safety maximum is rejected"
    );
    context.expect(
        invalid_target_fixture.heater.last_demand().percent() == 0.0F,
        "M4: rejected unsafe recipe cannot drive simulated heater"
    );

    Fixture live_target_fixture;
    context.expect(live_target_fixture.start(recipe(100.0F)), "M4: live-target session is queued");
    live_target_fixture.application.tick();
    context.expect(
        live_target_fixture.application.submit(
            SetChamberTargetCommand{temperature(151.0F)}
        ),
        "M4: unsafe live target reaches application validation"
    );
    live_target_fixture.application.tick();
    context.expect(
        live_target_fixture.application.snapshot().chamber_target
            && live_target_fixture.application.snapshot().chamber_target->celsius() == 100.0F,
        "M4: unsafe live target cannot override safety limits"
    );
}

void test_m5_complete_slice(TestContext& context)
{
    Fixture fixture;
    auto selected_recipe = recipe(100.0F, timer(10s, immediate()));
    context.expect(fixture.start(selected_recipe, 77U), "M5: end-to-end session is queued");
    fixture.application.tick();
    const float demand_before_probe_alarm = fixture.heater.last_demand().percent();

    static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    const auto* target_alarm = find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U);
    const auto target_alarm_id = target_alarm == nullptr
        ? std::optional<smoker::core::AlarmId>{}
        : std::optional<smoker::core::AlarmId>{target_alarm->id};
    context.expect(target_alarm != nullptr, "M5: food target creates an alarm");
    context.expect(
        demand_before_probe_alarm == 100.0F && snapshot.heater_demand.percent() == 100.0F,
        "M5: food target does not change chamber heater demand"
    );
    context.expect(
        snapshot.session_status == SessionStatus::Running,
        "M5: food target alarm does not stop the session"
    );
    context.expect(
        has_event(fixture.events.events(), EventType::ProbeTargetReached),
        "M5: food target publishes an event"
    );

    static_cast<void>(fixture.food_source.set_reading(2U, std::nullopt));
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(
        find_alarm(snapshot, AlarmCode::ProbeDisconnected, 2U) != nullptr,
        "M5: non-authoritative probe disconnection creates an alarm"
    );
    context.expect(!snapshot.active_fault, "M5: food-probe failure is not a control fault");
    context.expect(
        snapshot.heater_demand.percent() == 100.0F,
        "M5: disconnected food probe does not affect heater control"
    );

    context.expect(snapshot.probes.size() == 2U, "M5: snapshot exposes configured 1..N probes");
    context.expect(
        snapshot.probes[0].id == 1U && snapshot.probes[0].name == "Brisket"
            && snapshot.probes[0].role == ProbeRole::Meat
            && snapshot.probes[0].current_temperature
            && snapshot.probes[0].current_temperature->celsius() == 75.0F
            && snapshot.probes[0].target_temperature
            && snapshot.probes[0].target_temperature->celsius() == 70.0F
            && snapshot.probes[0].enabled && snapshot.probes[0].alarm_enabled,
        "BR-006: snapshot exposes every required food-probe property"
    );
    context.expect(
        snapshot.session_id == 77U && snapshot.recipe_snapshot
            && snapshot.chamber_temperature && snapshot.chamber_target,
        "M5: snapshot exposes session and chamber state"
    );
    context.expect(snapshot.timer.started, "M5: snapshot exposes timer state");

    if (target_alarm_id) {
        context.expect(
            fixture.application.submit(AcknowledgeAlarmCommand{*target_alarm_id}),
            "M5: alarm acknowledgement command is queued"
        );
        fixture.application.tick();
        snapshot = fixture.application.snapshot();
        const auto* acknowledged = find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U);
        context.expect(
            acknowledged != nullptr && acknowledged->acknowledged,
            "M5: alarm acknowledgement is visible in snapshots"
        );
    }

    context.expect(
        fixture.application.submit(SetProbeAlarmEnabledCommand{1U, false}),
        "M5: probe alarm enable command is queued"
    );
    context.expect(
        fixture.application.submit(SetProbeEnabledCommand{2U, false}),
        "M5: probe enable command is queued"
    );
    context.expect(
        fixture.application.submit(SetProbeTargetCommand{1U, temperature(80.0F)}),
        "M5: live probe target command is queued"
    );
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(
        !snapshot.probes[0].alarm_enabled && !snapshot.probes[1].enabled
            && snapshot.probes[0].target_temperature
            && snapshot.probes[0].target_temperature->celsius() == 80.0F,
        "M5: probe commands update application-owned state"
    );

    fixture.clock.advance(10s);
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(snapshot.timer.completed, "M5: simulated timer completes end-to-end");
    context.expect(
        find_alarm(snapshot, AlarmCode::TimerCompleted, std::nullopt) != nullptr,
        "M5: default timer completion raises notification alarm"
    );
    context.expect(
        has_event(fixture.events.events(), EventType::SessionStarted)
            && has_event(fixture.events.events(), EventType::TimerStarted)
            && has_event(fixture.events.events(), EventType::TimerCompleted),
        "M5: event sink observes session and timer flow"
    );

    context.expect(
        fixture.application.submit(StopSessionCommand{}),
        "M5: final Stop command is queued"
    );
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(
        snapshot.session_status == SessionStatus::Stopped
            && snapshot.heater_demand.percent() == 0.0F,
        "M5: complete simulated run ends with heater OFF"
    );
    context.expect(
        has_event(fixture.events.events(), EventType::SessionStopped),
        "M5: final Stop is published through the event flow"
    );
}

void test_m5_start_is_heap_quiet_and_rearms_target_alarm(TestContext& context)
{
    Fixture fixture;
    auto first_recipe = recipe(
        100.0F,
        std::nullopt,
        "A deliberately long first recipe name that cannot use string SSO"
    );
    first_recipe.stage.name =
        "A deliberately long first stage name that cannot use string SSO";
    context.expect(fixture.start(first_recipe, 101U), "M5: first long recipe is queued");

    allocation_probe::begin();
    fixture.application.tick();
    const auto first_start_counts = allocation_probe::end();
    context.expect(
        first_start_counts.allocations == 0U && first_start_counts.deallocations == 0U,
        "M5: first Start performs no observed ordinary C++ allocation in tick"
    );

    static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
    fixture.application.tick();
    auto first_snapshot = fixture.application.snapshot();
    const auto* first_alarm = find_alarm(first_snapshot, AlarmCode::ProbeTargetReached, 1U);
    const auto first_alarm_id = first_alarm == nullptr ? 0U : first_alarm->id;
    context.expect(first_alarm != nullptr, "M5: first session emits its target alarm");

    context.expect(
        fixture.application.submit(StopSessionCommand{}),
        "M5: first session Stop is queued"
    );
    fixture.application.tick();

    auto second_recipe = recipe(
        105.0F,
        std::nullopt,
        "A deliberately long second recipe name that cannot use string SSO"
    );
    second_recipe.stage.name =
        "A deliberately long second stage name that cannot use string SSO";
    context.expect(fixture.start(second_recipe, 102U), "M5: second long recipe is queued");

    allocation_probe::begin();
    fixture.application.tick();
    const auto replacement_start_counts = allocation_probe::end();
    context.expect(
        replacement_start_counts.allocations == 0U
            && replacement_start_counts.deallocations == 0U,
        "M5: replacement Start performs no observed ordinary C++ allocation in tick"
    );

    auto second_snapshot = fixture.application.snapshot();
    const auto* second_alarm = find_alarm(second_snapshot, AlarmCode::ProbeTargetReached, 1U);
    const auto second_alarm_id = second_alarm == nullptr ? 0U : second_alarm->id;
    context.expect(
        second_alarm != nullptr && !second_alarm->acknowledged
            && second_alarm->id != first_alarm_id,
        "M5: new Start resolves the prior alarm and raises a fresh session alarm"
    );

    fixture.application.tick();
    second_snapshot = fixture.application.snapshot();
    const auto* latched_alarm = find_alarm(second_snapshot, AlarmCode::ProbeTargetReached, 1U);
    context.expect(
        latched_alarm != nullptr && latched_alarm->id == second_alarm_id,
        "M5: target alarm remains latched once per session and target"
    );

    auto rejected_recipe = recipe(
        110.0F,
        std::nullopt,
        "A deliberately long rejected recipe name that cannot use string SSO"
    );
    rejected_recipe.stage.name =
        "A deliberately long rejected stage name that cannot use string SSO";
    context.expect(
        fixture.start(rejected_recipe, 103U),
        "M5: active-session Start reaches semantic rejection"
    );
    allocation_probe::begin();
    fixture.application.tick();
    const auto rejected_start_counts = allocation_probe::end();
    context.expect(
        rejected_start_counts.allocations == 0U
            && rejected_start_counts.deallocations == 0U,
        "M5: rejected Start performs no observed ordinary C++ allocation in tick"
    );
}

void test_m5_representative_tick_is_cpp_heap_quiet(TestContext& context)
{
    Fixture fixture;
    context.expect(
        fixture.start(recipe(100.0F), 104U),
        "M5: representative heap-observation session is queued"
    );
    fixture.application.tick();
    fixture.events.clear();

    static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
    context.expect(
        fixture.application.submit(SetChamberTargetCommand{temperature(105.0F)})
            && fixture.application.submit(SetProbeTargetCommand{1U, temperature(74.0F)}),
        "M5: representative commands are queued outside the measured tick"
    );

    allocation_probe::begin();
    fixture.application.tick();
    const auto tick_counts = allocation_probe::end();

    const auto snapshot = fixture.application.snapshot();
    context.expect(
        tick_counts.allocations == 0U && tick_counts.deallocations == 0U,
        "M5: command/event/alarm tick performs no observed ordinary C++ allocation"
    );
    context.expect(
        snapshot.chamber_target && snapshot.chamber_target->celsius() == 105.0F
            && find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U) != nullptr
            && has_event(fixture.events.events(), EventType::ChamberTargetChanged)
            && has_event(fixture.events.events(), EventType::ProbeTargetChanged)
            && has_event(fixture.events.events(), EventType::ProbeTargetReached),
        "M5: measured tick exercises commands, events, an alarm, safety, and heater output"
    );
}

void test_m5_same_cycle_command_semantics(TestContext& context)
{
    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: alarm-disable session is queued");
        fixture.application.tick();
        fixture.events.clear();

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        context.expect(
            fixture.application.submit(SetProbeAlarmEnabledCommand{1U, false}),
            "M5: same-cycle alarm disable is queued"
        );
        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U) == nullptr
                && !has_event(fixture.events.events(), EventType::ProbeTargetReached),
            "M5: alarm disable suppresses a threshold reached in the same cycle"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: probe-disable session is queued");
        fixture.application.tick();
        fixture.events.clear();

        static_cast<void>(fixture.food_source.set_reading(2U, std::nullopt));
        context.expect(
            fixture.application.submit(SetProbeEnabledCommand{2U, false}),
            "M5: same-cycle probe disable is queued"
        );
        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            find_alarm(snapshot, AlarmCode::ProbeDisconnected, 2U) == nullptr
                && !has_event(fixture.events.events(), EventType::ProbeDisconnected)
                && !snapshot.probes[1].enabled,
            "M5: probe disable suppresses same-cycle disconnect side effects"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: Stop/threshold session is queued");
        fixture.application.tick();
        fixture.events.clear();

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        context.expect(
            fixture.application.submit(StopSessionCommand{}),
            "M5: same-cycle Stop is queued"
        );
        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            snapshot.session_status == SessionStatus::Stopped
                && find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U) == nullptr
                && !has_event(fixture.events.events(), EventType::ProbeTargetReached),
            "M5: Stop preempts same-cycle probe-target alarm derivation"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: target-change session is queued");
        fixture.application.tick();

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        context.expect(
            fixture.application.submit(SetProbeTargetCommand{1U, temperature(80.0F)}),
            "M5: higher same-cycle target is queued"
        );
        fixture.application.tick();
        context.expect(
            find_alarm(
                fixture.application.snapshot(), AlarmCode::ProbeTargetReached, 1U
            ) == nullptr,
            "M5: the new higher target suppresses the old threshold in the same cycle"
        );

        fixture.events.clear();
        context.expect(
            fixture.application.submit(SetProbeTargetCommand{1U, temperature(74.0F)}),
            "M5: lower same-cycle target is queued"
        );
        fixture.application.tick();
        context.expect(
            find_alarm(
                fixture.application.snapshot(), AlarmCode::ProbeTargetReached, 1U
            ) != nullptr
                && has_event(fixture.events.events(), EventType::ProbeTargetReached),
            "M5: the new lower target raises its alarm in the same cycle"
        );
    }
}

void test_p0_sr_003_manual_stop_is_off_barrier(TestContext& context)
{
    Fixture fixture;
    context.expect(
        fixture.start(recipe(100.0F), 301U),
        "SR-003: initial session is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.heater.last_demand().percent() == 100.0F,
        "SR-003: precondition confirms the active session is heating"
    );

    fixture.events.clear();
    const auto writes_before_stop = fixture.heater.history().size();
    context.expect(
        fixture.application.submit(StopSessionCommand{})
            && fixture.start(recipe(110.0F), 302U),
        "SR-003: Stop followed by explicit Start fits the same command batch"
    );
    fixture.application.tick();

    const auto stopped = fixture.application.snapshot();
    context.expect(
        stopped.session_status == SessionStatus::Stopped
            && stopped.session_id == 301U
            && stopped.stop_reason == StopReason::User,
        "SR-003: manual Stop remains terminal for its control cycle"
    );
    context.expect(
        stopped.heater_demand.percent() == 0.0F
            && fixture.heater.history().size() == writes_before_stop + 1U
            && fixture.heater.history().back() == 0.0F,
        "SR-003/SF-010: Stop produces exactly one final safety-gated OFF write"
    );
    context.expect(
        has_event(fixture.events.events(), EventType::SessionStopped)
            && !has_event(fixture.events.events(), EventType::SessionStarted),
        "SR-003: commands after Stop are deferred beyond the OFF cycle"
    );

    fixture.events.clear();
    fixture.application.tick();
    const auto restarted = fixture.application.snapshot();
    context.expect(
        restarted.session_status == SessionStatus::Running
            && restarted.session_id == 302U
            && restarted.heater_demand.percent() == 100.0F
            && has_event(fixture.events.events(), EventType::SessionStarted),
        "SR-003: later FIFO commands resume only on the next explicit control cycle"
    );
}

void test_p0_stop_coalescing_preserves_fifo_intent(TestContext& context)
{
    {
        Fixture fixture;
        context.expect(
            fixture.application.submit(StopSessionCommand{})
                && fixture.start(recipe(100.0F), 311U)
                && fixture.application.submit(StopSessionCommand{}),
            "P0 Stop admission: interleaved IDLE Stop/Start/Stop commands are admitted"
        );

        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            snapshot.session_status == SessionStatus::Stopped
                && snapshot.session_id == 311U
                && snapshot.stop_reason == StopReason::User
                && snapshot.heater_demand.percent() == 0.0F,
            "P0 Stop admission: a Stop after an intervening Start is not coalesced away"
        );
        context.expect(
            has_event(fixture.events.events(), EventType::CommandRejected)
                && has_event(fixture.events.events(), EventType::SessionStarted)
                && has_event(fixture.events.events(), EventType::SessionStopped),
            "P0 Stop admission: IDLE sequence retains FIFO processing evidence"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F), 321U),
            "P0 Stop admission: initial RUNNING session is queued"
        );
        fixture.application.tick();
        context.expect(
            fixture.application.submit(StopSessionCommand{})
                && fixture.start(recipe(110.0F), 322U)
                && fixture.application.submit(StopSessionCommand{}),
            "P0 Stop admission: interleaved RUNNING Stop/Start/Stop commands are admitted"
        );

        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().session_status == SessionStatus::Stopped
                && fixture.application.snapshot().session_id == 321U
                && fixture.heater.last_demand().percent() == 0.0F,
            "P0 Stop admission: the first Stop retains its observable OFF-cycle barrier"
        );

        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            snapshot.session_status == SessionStatus::Stopped
                && snapshot.session_id == 322U
                && snapshot.stop_reason == StopReason::User
                && snapshot.heater_demand.percent() == 0.0F,
            "P0 Stop admission: the distinct later Stop prevents the queued Start from heating"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F), 331U),
            "P0 Stop coalescing: initial session is queued"
        );
        fixture.application.tick();
        fixture.events.clear();
        context.expect(
            fixture.application.submit(StopSessionCommand{})
                && fixture.application.submit(StopSessionCommand{}),
            "P0 Stop coalescing: consecutive Stops are accepted and coalesced"
        );

        fixture.application.tick();
        fixture.events.clear();
        fixture.application.tick();
        context.expect(
            !has_event(fixture.events.events(), EventType::CommandRejected)
                && fixture.application.snapshot().session_status == SessionStatus::Stopped,
            "P0 Stop coalescing: no redundant consecutive Stop remains for a later cycle"
        );
    }
}

void test_p0_cr_005_heating_state_invariants(TestContext& context)
{
    Fixture fixture;
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Idle
            && fixture.heater.last_demand().percent() == 0.0F,
        "CR-005: IDLE always writes heater OFF"
    );

    auto monitoring_recipe = recipe(100.0F);
    monitoring_recipe.stage.chamber_target.reset();
    context.expect(
        fixture.start(monitoring_recipe, 401U),
        "CR-002: monitoring-only session is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Running
            && !fixture.application.snapshot().chamber_target
            && fixture.heater.last_demand().percent() == 0.0F,
        "CR-002/CR-005: RUNNING without a chamber target always writes heater OFF"
    );

    context.expect(
        fixture.application.submit(StopSessionCommand{}),
        "CR-005: monitoring session Stop is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Stopped
            && fixture.heater.last_demand().percent() == 0.0F,
        "CR-005: STOPPED always writes heater OFF"
    );

    context.expect(
        fixture.start(recipe(100.0F), 402U),
        "CR-005: heating session is explicitly queued"
    );
    fixture.application.tick();
    fixture.chamber.set_reading(std::nullopt);
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Fault
            && fixture.heater.last_demand().percent() == 0.0F,
        "CR-005/SF-002: FAULT always writes heater OFF"
    );

    fixture.chamber.set_reading(temperature(20.0F));
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Fault
            && fixture.heater.last_demand().percent() == 0.0F,
        "SF-007: recovered input neither clears the fault nor resumes heating"
    );
    context.expect(
        fixture.application.submit(ClearResolvedFaultCommand{}),
        "SF-007: resolved fault clear is queued"
    );
    fixture.application.tick();
    context.expect(
        fixture.application.snapshot().session_status == SessionStatus::Stopped
            && !fixture.application.snapshot().active_fault
            && fixture.heater.last_demand().percent() == 0.0F,
        "SF-007: clearing a resolved fault leaves the session STOPPED and heater OFF"
    );
}

void test_m5_alarm_lifecycle_and_probe_defaults(TestContext& context)
{
    {
        Fixture fixture;
        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        static_cast<void>(fixture.food_source.set_reading(2U, std::nullopt));
        fixture.application.tick();
        const auto idle_snapshot = fixture.application.snapshot();
        context.expect(
            idle_snapshot.session_status == SessionStatus::Idle
                && idle_snapshot.active_alarms.empty(),
            "M5: IDLE may observe probe connectivity but raises no probe alarms"
        );

        fixture.events.clear();
        context.expect(
            fixture.application.submit(SetProbeEnabledCommand{1U, false})
                && fixture.application.submit(SetProbeAlarmEnabledCommand{1U, false}),
            "M5: idle probe-session commands reach semantic validation"
        );
        fixture.application.tick();
        const auto idle_rejection_count = static_cast<std::size_t>(std::count_if(
            fixture.events.events().begin(),
            fixture.events.events().end(),
            [](const smoker::core::Event& event) {
                return event.type == EventType::CommandRejected;
            }
        ));
        context.expect(
            idle_rejection_count == 2U && fixture.application.snapshot().probes[0].enabled,
            "M5: probe session settings cannot be mutated outside RUNNING"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: reconnect session is queued");
        fixture.application.tick();
        fixture.events.clear();

        static_cast<void>(fixture.food_source.set_reading(2U, std::nullopt));
        fixture.application.tick();
        context.expect(
            find_alarm(
                fixture.application.snapshot(), AlarmCode::ProbeDisconnected, 2U
            ) != nullptr,
            "M5: disconnect creates an active alarm while RUNNING"
        );

        fixture.events.clear();
        static_cast<void>(fixture.food_source.set_reading(2U, temperature(20.0F)));
        fixture.application.tick();
        context.expect(
            find_alarm(
                fixture.application.snapshot(), AlarmCode::ProbeDisconnected, 2U
            ) == nullptr
                && has_event(fixture.events.events(), EventType::ProbeReconnected),
            "M5: reconnect resolves the disconnect alarm and emits ProbeReconnected"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: alarm lifecycle session is queued");
        fixture.application.tick();
        static_cast<void>(fixture.food_source.set_reading(1U, temperature(75.0F)));
        fixture.application.tick();

        auto snapshot = fixture.application.snapshot();
        const auto* alarm = find_alarm(snapshot, AlarmCode::ProbeTargetReached, 1U);
        context.expect(alarm != nullptr, "M5: target alarm is active before acknowledgement");
        if (alarm != nullptr) {
            context.expect(
                fixture.application.submit(AcknowledgeAlarmCommand{alarm->id}),
                "M5: target acknowledgement is queued"
            );
            fixture.application.tick();
            const auto acknowledged_snapshot = fixture.application.snapshot();
            const auto* acknowledged = find_alarm(
                acknowledged_snapshot, AlarmCode::ProbeTargetReached, 1U
            );
            context.expect(
                acknowledged != nullptr && acknowledged->acknowledged,
                "M5: acknowledgement is distinct from lifecycle resolution"
            );
        }

        context.expect(
            fixture.application.submit(StopSessionCommand{}),
            "M5: lifecycle Stop is queued"
        );
        fixture.application.tick();
        context.expect(
            find_alarm(
                fixture.application.snapshot(), AlarmCode::ProbeTargetReached, 1U
            ) == nullptr,
            "M5: ending the session resolves its probe-target alarm"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F), 201U), "M5: defaults session is queued");
        fixture.application.tick();
        context.expect(
            fixture.application.submit(SetProbeTargetCommand{1U, temperature(80.0F)}),
            "M5: live probe target is queued"
        );
        fixture.application.tick();
        context.expect(
            fixture.application.submit(StopSessionCommand{}),
            "M5: defaults session Stop is queued"
        );
        fixture.application.tick();

        static_cast<void>(fixture.food_source.set_reading(1U, temperature(60.0F)));
        context.expect(fixture.start(recipe(105.0F), 202U), "M5: next session is queued");
        fixture.application.tick();
        const auto next_snapshot = fixture.application.snapshot();
        context.expect(
            next_snapshot.probes[0].target_temperature
                && next_snapshot.probes[0].target_temperature->celsius() == 70.0F,
            "M5: live probe target does not modify the next session's defaults"
        );
    }
}

void test_m5_bounded_event_sink(TestContext& context)
{
    smoker::platform::SimulatedEventSink sink;
    constexpr std::size_t overflow = 7U;

    allocation_probe::begin();
    for (std::size_t index = 0U;
         index < smoker::platform::SimulatedEventSink::event_capacity + overflow;
         ++index) {
        sink.publish(smoker::core::Event{
            EventType::CommandRejected,
            monotonic_time(index),
            std::nullopt,
            std::nullopt,
            std::nullopt,
        });
    }
    const auto publish_counts = allocation_probe::end();

    const auto events = sink.events();
    context.expect(
        publish_counts.allocations == 0U && publish_counts.deallocations == 0U,
        "M5: event publishing performs no observed ordinary C++ allocation after capacity"
    );
    context.expect(
        events.size() == smoker::platform::SimulatedEventSink::event_capacity,
        "M5: event history remains bounded at its fixed capacity"
    );
    context.expect(
        sink.overwritten_event_count() == overflow,
        "M5: event-history overflow is explicit and observable"
    );
    context.expect(
        !events.empty() && events.front().occurred_at == monotonic_time(overflow)
            && events.back().occurred_at
                == monotonic_time(
                    smoker::platform::SimulatedEventSink::event_capacity + overflow - 1U
                ),
        "M5: bounded event history retains the newest events in chronological order"
    );
}

void test_m5_validation_queue_and_combined_order(TestContext& context)
{
    {
        std::array<FoodProbeConfig, 0U> no_probes{};
        smoker::platform::SimulatedChamberSensor chamber{temperature(20.0F)};
        smoker::platform::SimulatedFoodProbeSource food_source{no_probes};
        smoker::platform::DeterministicChamberController chamber_controller;
        smoker::platform::SimulatedHeaterOutput heater;
        smoker::platform::SimulatedClock clock;
        smoker::platform::SimulatedEventSink events;
        SmokerApplication application{
            chamber,
            food_source,
            chamber_controller,
            heater,
            clock,
            events,
            smoker::core::SafetyLimits{temperature(150.0F)},
            no_probes,
        };
        application.tick();
        const auto snapshot = application.snapshot();
        context.expect(
            snapshot.active_fault
                && snapshot.active_fault->code == FaultCode::ConfigurationInvalid
                && snapshot.heater_demand.percent() == 0.0F,
            "M5: empty probe configuration fails closed"
        );
    }

    {
        auto duplicate_probes = probe_configuration();
        duplicate_probes[1].id = duplicate_probes[0].id;
        smoker::platform::SimulatedChamberSensor chamber{temperature(20.0F)};
        smoker::platform::SimulatedFoodProbeSource food_source{duplicate_probes};
        smoker::platform::DeterministicChamberController chamber_controller;
        smoker::platform::SimulatedHeaterOutput heater;
        smoker::platform::SimulatedClock clock;
        smoker::platform::SimulatedEventSink events;
        SmokerApplication application{
            chamber,
            food_source,
            chamber_controller,
            heater,
            clock,
            events,
            smoker::core::SafetyLimits{temperature(150.0F)},
            duplicate_probes,
        };
        application.tick();
        context.expect(
            application.snapshot().active_fault
                && application.snapshot().active_fault->code
                    == FaultCode::ConfigurationInvalid,
            "M5: duplicate probe IDs fail configuration validation"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(100.0F, timer(0ms, immediate()))),
            "M5: invalid zero-duration timer reaches validation"
        );
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().session_status == SessionStatus::Idle,
            "M5: invalid timer configuration is rejected"
        );
        context.expect(
            fixture.start(recipe(100.0F, timer(1s, probe_at(99U, 70.0F)))),
            "M5: unknown timer probe reaches validation"
        );
        fixture.application.tick();
        context.expect(
            fixture.application.snapshot().session_status == SessionStatus::Idle,
            "M5: timer referencing an unknown probe is rejected"
        );
    }

    {
        Fixture fixture;
        context.expect(fixture.start(recipe(100.0F)), "M5: queue-contract session is queued");
        fixture.application.tick();
        fixture.events.clear();

        std::size_t accepted = 0U;
        for (std::size_t index = 0U; index < 15U; ++index) {
            if (fixture.application.submit(SetChamberTargetCommand{temperature(90.0F)})) {
                ++accepted;
            }
        }
        const bool overflow_accepted = fixture.application.submit(
            SetChamberTargetCommand{temperature(95.0F)}
        );
        const bool stop_accepted = fixture.application.submit(StopSessionCommand{});
        const bool duplicate_stop_coalesced = fixture.application.submit(StopSessionCommand{});
        fixture.application.tick();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            accepted == 15U && !overflow_accepted && stop_accepted
                && duplicate_stop_coalesced,
            "M5: regular commands cannot consume the reserved Stop admission"
        );
        context.expect(
            snapshot.session_status == SessionStatus::Stopped
                && snapshot.command_queue_overflow_count == 1U
                && has_event(fixture.events.events(), EventType::CommandQueueOverflow),
            "M5: Stop is processed and regular-command overflow is observable"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(
                100.0F,
                timer(1s, immediate(), TimerCompletionAction::StopSession)
            )),
            "M5: combined timer/fault session is queued"
        );
        fixture.application.tick();
        fixture.events.clear();
        fixture.clock.advance(1s);
        fixture.chamber.set_reading(std::nullopt);
        fixture.application.tick();

        const auto events = fixture.events.events();
        const auto snapshot = fixture.application.snapshot();
        context.expect(
            events.size() == 3U && events[0].type == EventType::TimerCompleted
                && events[1].type == EventType::SessionStopped
                && events[2].type == EventType::FaultRaised,
            "M5: simultaneous timer completion and fault publish deterministic order"
        );
        context.expect(
            snapshot.session_status == SessionStatus::Fault
                && snapshot.stop_reason == StopReason::TimerCompleted
                && snapshot.heater_demand.percent() == 0.0F,
            "M5: simultaneous timer completion and fault fail safe in the same cycle"
        );
    }

    {
        Fixture fixture;
        context.expect(
            fixture.start(recipe(
                100.0F,
                timer(1s, immediate(), TimerCompletionAction::StopSession)
            )),
            "M5: combined manual Stop/fault session is queued"
        );
        fixture.application.tick();
        fixture.events.clear();
        context.expect(
            fixture.application.submit(StopSessionCommand{}),
            "M5: simultaneous manual Stop is queued"
        );
        fixture.clock.advance(1s);
        fixture.chamber.set_reading(std::nullopt);
        fixture.application.tick();

        const auto events = fixture.events.events();
        context.expect(
            events.size() == 2U && events[0].type == EventType::SessionStopped
                && events[1].type == EventType::FaultRaised
                && !has_event(events, EventType::TimerCompleted),
            "M5: manual Stop deterministically preempts timer before fault evaluation"
        );
        context.expect(
            fixture.application.snapshot().session_status == SessionStatus::Fault
                && fixture.application.snapshot().stop_reason == StopReason::User
                && fixture.heater.last_demand().percent() == 0.0F,
            "M5: simultaneous Stop and fault preserve reason and force heater OFF"
        );
    }
}

void test_m5_command_result_correlation(TestContext& context)
{
    Fixture fixture;
    context.expect(
        fixture.application.submit(StartSessionCommand{10U, recipe(100.0F)}, 501U),
        "M5: correlated start reaches the application queue"
    );
    fixture.application.tick();
    auto snapshot = fixture.application.snapshot();
    context.expect(
        snapshot.command_results.size() == 1U
            && snapshot.command_results.back().correlation_id == 501U
            && snapshot.command_results.back().semantic_accepted,
        "M5: accepted command publishes its semantic result and correlation id"
    );

    context.expect(
        fixture.application.submit(StartSessionCommand{11U, recipe(100.0F)}, 502U),
        "M5: duplicate correlated start reaches semantic validation"
    );
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    context.expect(
        snapshot.command_results.size() == 2U
            && snapshot.command_results.back().correlation_id == 502U
            && !snapshot.command_results.back().semantic_accepted,
        "M5: rejected command publishes a distinct semantic rejection"
    );

    context.expect(
        fixture.application.submit(StopSessionCommand{}, 503U)
            && fixture.application.submit(StopSessionCommand{}, 504U),
        "M5: consecutive correlated Stops retain coalesced admission"
    );
    fixture.application.tick();
    snapshot = fixture.application.snapshot();
    const auto has_result = [&snapshot](const std::uint32_t id) {
        return std::any_of(
            snapshot.command_results.begin(), snapshot.command_results.end(),
            [id](const smoker::app::CommandResultView& result) {
                return result.correlation_id == id && result.semantic_accepted;
            }
        );
    };
    context.expect(
        has_result(503U) && has_result(504U),
        "M5: both queued and coalesced Stop IDs receive semantic results"
    );

    Fixture idle_fixture;
    context.expect(
        idle_fixture.application.submit(StopSessionCommand{}, 601U)
            && idle_fixture.application.submit(StopSessionCommand{}, 602U),
        "M5: consecutive correlated Stops coalesce before semantic validation"
    );
    idle_fixture.application.tick();
    const auto idle_snapshot = idle_fixture.application.snapshot();
    const auto rejected_stop_result = [&idle_snapshot](const std::uint32_t id) {
        return std::any_of(
            idle_snapshot.command_results.begin(),
            idle_snapshot.command_results.end(),
            [id](const smoker::app::CommandResultView& result) {
                return result.correlation_id == id
                    && !result.semantic_accepted;
            }
        );
    };
    context.expect(
        rejected_stop_result(601U) && rejected_stop_result(602U),
        "M5: a coalesced Stop inherits the original Stop semantic rejection"
    );
}

} // namespace

int main(const int argc, const char* const argv[])
{
    TestContext context;
    const std::string_view group = argc > 1 ? argv[1] : "all";
    const bool known_group = group == "m2" || group == "m3" || group == "m4"
        || group == "m5" || group == "all";
    if (argc > 2 || !known_group) {
        std::cerr << "usage: smoker_v0_tests [m2|m3|m4|m5|all]\n";
        return 2;
    }

    if (group == "m2" || group == "all") {
        test_m2(context);
    }
    if (group == "m3" || group == "all") {
        test_m3_session_and_snapshot(context);
        test_m3_timer_conditions(context);
        test_m3_probe_timer_availability(context);
    }
    if (group == "m4" || group == "all") {
        test_m4_invalid_and_latched_fault(context);
        test_m4_over_temperature_and_limits(context);
    }
    if (group == "m5" || group == "all") {
        test_m5_complete_slice(context);
        test_m5_start_is_heap_quiet_and_rearms_target_alarm(context);
        test_m5_representative_tick_is_cpp_heap_quiet(context);
        test_m5_same_cycle_command_semantics(context);
        test_p0_sr_003_manual_stop_is_off_barrier(context);
        test_p0_stop_coalescing_preserves_fifo_intent(context);
        test_p0_cr_005_heating_state_invariants(context);
        test_m5_alarm_lifecycle_and_probe_defaults(context);
        test_m5_bounded_event_sink(context);
        test_m5_validation_queue_and_combined_order(context);
        test_m5_command_result_correlation(context);
    }

    if (context.failure_count() != 0) {
        std::cerr << context.failure_count() << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "Smoker V0 " << group << " tests passed\n";
    return 0;
}

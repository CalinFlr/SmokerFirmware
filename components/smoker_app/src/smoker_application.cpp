#include "smoker/app/smoker_application.hpp"

#include "smoker/core/safety.hpp"
#include "smoker/core/timer.hpp"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace smoker::app {

SmokerApplication::SmokerApplication(
    IChamberSensor& chamber_sensor,
    IFoodProbeSource& food_probe_source,
    IChamberController& chamber_controller,
    IHeaterOutput& heater_output,
    IClock& clock,
    IEventSink& event_sink,
    const core::SafetyLimits safety_limits,
    const std::span<const core::FoodProbeConfig> probe_configuration
)
    : chamber_sensor_{chamber_sensor}
    , food_probe_source_{food_probe_source}
    , chamber_controller_{chamber_controller}
    , heater_output_{heater_output}
    , clock_{clock}
    , event_sink_{event_sink}
    , safety_limits_{safety_limits}
{
    probes_.reserve(probe_configuration.size());
    probe_readings_.reserve(probe_configuration.size());
    alarms_.reserve((probe_configuration.size() * 2U) + 1U);
    probe_snapshot_views_.reserve(probe_configuration.size());
    active_alarm_snapshot_.reserve((probe_configuration.size() * 2U) + 1U);
    pending_events_.reserve((probe_configuration.size() * 3U) + (command_capacity * 2U) + 8U);
    command_results_.reserve(command_result_capacity);

    configuration_valid_ = !probe_configuration.empty();
    for (const auto& configuration : probe_configuration) {
        if (probe_exists(configuration.id)) {
            configuration_valid_ = false;
        }
        probes_.push_back(ProbeRuntime{
            configuration,
            configuration.target_temperature,
            configuration.enabled,
            configuration.alarm_enabled,
            std::nullopt,
            false,
            false,
            false,
            false,
            false,
        });
        probe_readings_.push_back(core::ProbeReading{configuration.id, std::nullopt});
    }

    refresh_snapshot_view_cache();

    // Establish the application-visible OFF command before the first
    // controller callback. A future real heater-output driver must still own
    // safe electrical initialization before application construction.
    heater_output_.write(core::HeaterDemand::off());
    controller_fault_resolved_ = reset_chamber_controller();
    controller_failure_pending_ = !controller_fault_resolved_;
    event_sink_.publish(core::Event{
        core::EventType::DeviceBooted,
        clock_.now(),
        std::nullopt,
        std::nullopt,
        std::nullopt,
    });
}

bool SmokerApplication::submit(
    Command command, const std::uint32_t correlation_id
)
{
    const bool is_stop = std::holds_alternative<StopSessionCommand>(command);
    if (is_stop && trailing_stop_is_pending()) {
        if (correlation_id != 0U) {
            const auto index =
                (command_tail_ + command_capacity - 1U) % command_capacity;
            auto& trailing = *commands_[index];
            if (trailing.coalesced_correlation_count
                == trailing.coalesced_correlation_ids.size()) {
                std::move(
                    trailing.coalesced_correlation_ids.begin() + 1,
                    trailing.coalesced_correlation_ids.end(),
                    trailing.coalesced_correlation_ids.begin()
                );
                --trailing.coalesced_correlation_count;
            }
            trailing.coalesced_correlation_ids[
                trailing.coalesced_correlation_count++
            ] = correlation_id;
        }
        return true;
    }

    const auto admission_limit = is_stop ? command_capacity : regular_command_capacity;
    if (command_count_ >= admission_limit) {
        ++command_queue_overflow_count_;
        record_command_result(correlation_id, false);
        return false;
    }

    commands_[command_tail_] = QueuedCommand{std::move(command), correlation_id};
    command_tail_ = (command_tail_ + 1U) % command_capacity;
    ++command_count_;
    return true;
}

void SmokerApplication::tick()
{
    const auto now = clock_.now();
    pending_events_.clear();

    if (reported_command_queue_overflow_count_ != command_queue_overflow_count_) {
        emit(core::EventType::CommandQueueOverflow, now);
        reported_command_queue_overflow_count_ = command_queue_overflow_count_;
    }

    acquire_raw_inputs();
    process_pending_commands(now);
    evaluate_probe_states(now);
    update_active_timer(now);

    session_elapsed_ = core::Duration{};
    if (session_) {
        const auto endpoint = session_->stopped_at.value_or(now);
        if (endpoint >= session_->started_at) {
            session_elapsed_ = endpoint - session_->started_at;
        }
    }

    core::HeaterDemand requested_demand = core::HeaterDemand::off();
    const bool controller_fault_is_latched = active_fault_
        && active_fault_->code == core::FaultCode::ControlLoopFailure;
    if ((controller_failure_pending_ || controller_fault_is_latched)
        && !controller_fault_resolved_) {
        controller_fault_resolved_ = reset_chamber_controller();
    }
    if (controller_is_eligible()) {
        const auto request = chamber_controller_.request(
            *chamber_temperature_, *session_->active_chamber_target
        );
        if (request) {
            requested_demand = *request;
            controller_active_ = true;
            controller_fault_resolved_ = false;
        } else {
            controller_failure_pending_ = true;
            controller_active_ = false;
            controller_fault_resolved_ = reset_chamber_controller();
        }
    } else if (controller_active_) {
        controller_active_ = false;
        controller_fault_resolved_ = reset_chamber_controller();
        if (!controller_fault_resolved_) {
            controller_failure_pending_ = true;
        }
    }

    // The controller produces only a request. Safety is evaluated after that
    // computation and remains authoritative before the sole final write.
    evaluate_safety(now);

    // A safety fault can make a controller ineligible after its request was
    // calculated in this cycle. Clear latent state before commanding OFF.
    if (controller_active_ && !controller_is_eligible()) {
        controller_active_ = false;
        controller_fault_resolved_ = reset_chamber_controller();
        if (!controller_fault_resolved_) {
            controller_failure_pending_ = true;
        }
    }

    // Do not replace a more authoritative fault. A controller failure masked
    // by one remains pending and prevents another request until it can be
    // latched explicitly as ControlLoopFailure.
    if (!active_fault_ && controller_failure_pending_) {
        raise_fault(core::FaultCode::ControlLoopFailure, now);
        controller_failure_pending_ = false;
    }

    heater_demand_ = core::apply_safety_gate(
        requested_demand, current_session_status(), active_fault_
    );
    heater_output_.write(heater_demand_);
    publish_pending_events();
    refresh_snapshot_view_cache();
}

SmokerSnapshot SmokerApplication::snapshot() const
{
    SmokerSnapshot result;
    result.session_status = current_session_status();
    result.chamber_temperature = chamber_temperature_;
    result.heater_demand = heater_demand_;
    result.active_fault = active_fault_;
    result.firmware_update_active = firmware_update_active_;
    result.command_queue_overflow_count = command_queue_overflow_count_;
    result.probes.reserve(probes_.size());
    result.active_alarms.reserve(alarms_.size());
    result.command_results = command_results_;

    if (session_) {
        result.session_id = session_->id;
        result.session_elapsed = session_elapsed_;
        result.recipe_snapshot = session_->recipe_snapshot;
        result.stop_reason = session_->stop_reason;
        result.chamber_target = session_->active_chamber_target;
        result.timer_configured = session_->recipe_snapshot.stage.timer.has_value();
        result.timer = session_->timer;
    }

    for (const auto& probe : probes_) {
        result.probes.push_back(ProbeSnapshot{
            probe.default_configuration.id,
            probe.default_configuration.name,
            probe.default_configuration.role,
            probe.current_temperature,
            probe.session_target_temperature,
            probe.session_enabled,
            probe.session_alarm_enabled,
        });
    }

    for (const auto& alarm : alarms_) {
        if (!alarm.resolved) {
            result.active_alarms.push_back(alarm);
        }
    }

    return result;
}

SmokerSnapshotView SmokerApplication::snapshot_view() const noexcept
{
    SmokerSnapshotView result{
        current_session_status(),
        std::nullopt,
        session_elapsed_,
        core::StopReason::None,
        chamber_temperature_,
        std::nullopt,
        heater_demand_,
        false,
        {},
        std::span<const ProbeSnapshotView>{probe_snapshot_views_},
        std::span<const core::Alarm>{active_alarm_snapshot_},
        active_fault_,
        firmware_update_active_,
        safety_limits_.maximum_chamber_temperature,
        command_queue_overflow_count_,
        std::span<const CommandResultView>{command_results_},
    };
    if (session_) {
        result.session_id = session_->id;
        result.stop_reason = session_->stop_reason;
        result.chamber_target = session_->active_chamber_target;
        result.timer_configured = session_->recipe_snapshot.stage.timer.has_value();
        result.timer = session_->timer;
    }
    return result;
}

void SmokerApplication::process_pending_commands(const core::MonotonicTimePoint now)
{
    while (command_count_ > 0U) {
        auto& queued = commands_[command_head_];
        const bool manual_stop_barrier = queued
            && std::holds_alternative<StopSessionCommand>(queued->command)
            && session_ && session_->status == core::SessionStatus::Running;
        current_command_rejected_ = false;
        if (queued) {
            std::visit(
                [this, now](auto& value) { process(value, now); }, queued->command
            );
            record_command_result(
                queued->correlation_id, !current_command_rejected_
            );
            for (std::size_t index = 0U;
                 index < queued->coalesced_correlation_count;
                 ++index) {
                record_command_result(
                    queued->coalesced_correlation_ids[index],
                    !current_command_rejected_
                );
            }
        }

        // Leave the processed value in its slot. The next submit() overwrites it
        // outside tick(), so destroying any retained dynamic command data never
        // becomes work of the critical control cycle.
        command_head_ = (command_head_ + 1U) % command_capacity;
        --command_count_;

        // SR-003 requires an accepted manual Stop to produce an observable OFF
        // control cycle. Preserve later FIFO commands for the next tick so a
        // queued Start cannot replace the stopped session before the final
        // safety-gated heater write below.
        if (manual_stop_barrier) {
            break;
        }
    }
}

void SmokerApplication::process(
    StartSessionCommand& command, const core::MonotonicTimePoint now
)
{
    if (active_fault_ || firmware_update_active_
        || (session_ && session_->status == core::SessionStatus::Running)
        || !recipe_is_valid(command.recipe)) {
        reject_command(now);
        return;
    }

    const auto active_chamber_target = command.recipe.stage.chamber_target;
    if (session_) {
        // Retain the previous recipe in the queue slot until a later submit()
        // overwrites that slot outside tick(). Swapping also transfers the new
        // recipe snapshot without allocating or freeing its strings here.
        std::swap(session_->recipe_snapshot, command.recipe);
    } else {
        session_.emplace();
        session_->recipe_snapshot = std::move(command.recipe);
    }

    session_->id = command.session_id;
    session_->status = core::SessionStatus::Running;
    session_->started_at = now;
    session_->stopped_at.reset();
    session_->active_chamber_target = active_chamber_target;
    session_->timer = core::TimerRuntimeState{};
    session_->stop_reason = core::StopReason::None;

    // A new explicit Start gets a fresh copy of the immutable device defaults.
    // Live session changes never flow back into those defaults.
    resolve_all_alarms();
    reset_probe_session_configuration();
    for (auto& probe : probes_) {
        probe.target_alarm_emitted = false;
        probe.disconnected_alarm_emitted = false;
    }
    emit(core::EventType::SessionStarted, now);
}

void SmokerApplication::process(
    const StopSessionCommand&, const core::MonotonicTimePoint now
)
{
    if (!session_ || session_->status != core::SessionStatus::Running) {
        reject_command(now);
        return;
    }

    stop_running_session(core::StopReason::User, now);
}

void SmokerApplication::process(
    const SetChamberTargetCommand& command, const core::MonotonicTimePoint now
)
{
    if (!session_ || session_->status != core::SessionStatus::Running
        || (command.target && *command.target > safety_limits_.maximum_chamber_temperature)) {
        reject_command(now);
        return;
    }

    session_->active_chamber_target = command.target;
    emit(core::EventType::ChamberTargetChanged, now);
}

void SmokerApplication::process(
    const SetProbeTargetCommand& command, const core::MonotonicTimePoint now
)
{
    auto* probe = find_probe(command.probe_id);
    if (!session_ || session_->status != core::SessionStatus::Running || probe == nullptr) {
        reject_command(now);
        return;
    }

    probe->session_target_temperature = command.target;
    probe->target_alarm_emitted = false;
    resolve_probe_alarms(core::AlarmCode::ProbeTargetReached, command.probe_id);
    emit(core::EventType::ProbeTargetChanged, now, command.probe_id);
}

void SmokerApplication::process(
    const SetProbeEnabledCommand& command, const core::MonotonicTimePoint now
)
{
    auto* probe = find_probe(command.probe_id);
    if (!session_ || session_->status != core::SessionStatus::Running || probe == nullptr) {
        reject_command(now);
        return;
    }

    probe->session_enabled = command.enabled;
    probe->reading_sampled = false;
    probe->reading_initialized = false;
    probe->connected = false;
    probe->target_alarm_emitted = false;
    probe->disconnected_alarm_emitted = false;
    if (!command.enabled) {
        probe->current_temperature.reset();
        resolve_probe_alarms(core::AlarmCode::ProbeTargetReached, command.probe_id);
        resolve_probe_alarms(core::AlarmCode::ProbeDisconnected, command.probe_id);
        for (auto& reading : probe_readings_) {
            if (reading.id == command.probe_id) {
                reading.temperature.reset();
                break;
            }
        }
    }
}

void SmokerApplication::process(
    const SetProbeAlarmEnabledCommand& command, const core::MonotonicTimePoint now
)
{
    auto* probe = find_probe(command.probe_id);
    if (!session_ || session_->status != core::SessionStatus::Running || probe == nullptr) {
        reject_command(now);
        return;
    }

    probe->session_alarm_enabled = command.enabled;
    probe->target_alarm_emitted = false;
    probe->disconnected_alarm_emitted = false;
    if (!command.enabled) {
        resolve_probe_alarms(core::AlarmCode::ProbeTargetReached, command.probe_id);
        resolve_probe_alarms(core::AlarmCode::ProbeDisconnected, command.probe_id);
    }
}

void SmokerApplication::process(
    const AcknowledgeAlarmCommand& command, const core::MonotonicTimePoint now
)
{
    const auto alarm = std::find_if(
        alarms_.begin(), alarms_.end(),
        [&command](const core::Alarm& value) {
            return value.id == command.alarm_id && !value.resolved;
        }
    );
    if (alarm == alarms_.end() || alarm->acknowledged) {
        reject_command(now);
        return;
    }

    alarm->acknowledged = true;
    emit(core::EventType::AlarmAcknowledged, now, alarm->probe_id, alarm->id);
}

void SmokerApplication::process(
    const ClearResolvedFaultCommand&, const core::MonotonicTimePoint now
)
{
    if (!active_fault_ || !fault_condition_is_resolved(active_fault_->code)) {
        reject_command(now);
        return;
    }

    const auto acknowledged_code = active_fault_->code;
    active_fault_.reset();
    if (session_ && session_->status == core::SessionStatus::Fault) {
        session_->status = core::SessionStatus::Stopped;
        session_->stopped_at = now;
        session_->stop_reason = core::StopReason::Fault;
    }
    emit(core::EventType::FaultAcknowledged, now, std::nullopt, std::nullopt, acknowledged_code);
}

void SmokerApplication::process(
    const PrepareFirmwareUpdateCommand&, const core::MonotonicTimePoint now
)
{
    if (firmware_update_active_
        || current_session_status() == core::SessionStatus::Running) {
        reject_command(now);
        return;
    }
    firmware_update_active_ = true;
}

void SmokerApplication::process(
    const FinishFirmwareUpdateCommand&, const core::MonotonicTimePoint now
)
{
    if (!firmware_update_active_) {
        reject_command(now);
        return;
    }
    firmware_update_active_ = false;
}

bool SmokerApplication::recipe_is_valid(const core::Recipe& recipe) const noexcept
{
    if (recipe.stage.chamber_target
        && *recipe.stage.chamber_target > safety_limits_.maximum_chamber_temperature) {
        return false;
    }

    if (!recipe.stage.timer) {
        return true;
    }

    if (!core::is_valid_timer_configuration(*recipe.stage.timer)) {
        return false;
    }

    const auto& condition = recipe.stage.timer->start_condition;
    return condition.type != core::TimerStartConditionType::ProbeTemperatureAtLeast
        || (condition.probe_id && probe_exists(*condition.probe_id));
}

bool SmokerApplication::probe_exists(const core::ProbeId probe_id) const noexcept
{
    return std::any_of(
        probes_.begin(), probes_.end(),
        [probe_id](const ProbeRuntime& probe) {
            return probe.default_configuration.id == probe_id;
        }
    );
}

bool SmokerApplication::fault_condition_is_resolved(const core::FaultCode code) const noexcept
{
    switch (code) {
    case core::FaultCode::ChamberSensorInvalid:
        return chamber_temperature_.has_value();
    case core::FaultCode::ChamberOverTemperature:
        return chamber_temperature_
            && *chamber_temperature_ <= safety_limits_.maximum_chamber_temperature;
    case core::FaultCode::ControlLoopFailure:
        return controller_fault_resolved_;
    case core::FaultCode::ConfigurationInvalid:
        return false;
    }

    return false;
}

core::SessionStatus SmokerApplication::current_session_status() const noexcept
{
    if (active_fault_) {
        return core::SessionStatus::Fault;
    }
    return session_ ? session_->status : core::SessionStatus::Idle;
}

SmokerApplication::ProbeRuntime* SmokerApplication::find_probe(const core::ProbeId probe_id) noexcept
{
    const auto probe = std::find_if(
        probes_.begin(), probes_.end(),
        [probe_id](const ProbeRuntime& value) {
            return value.default_configuration.id == probe_id;
        }
    );
    return probe == probes_.end() ? nullptr : &*probe;
}

bool SmokerApplication::trailing_stop_is_pending() const noexcept
{
    if (command_count_ == 0U) {
        return false;
    }

    const auto index = (command_tail_ + command_capacity - 1U) % command_capacity;
    return commands_[index]
        && std::holds_alternative<StopSessionCommand>(commands_[index]->command);
}

void SmokerApplication::reset_probe_session_configuration() noexcept
{
    for (auto& probe : probes_) {
        probe.session_target_temperature = probe.default_configuration.target_temperature;
        probe.session_enabled = probe.default_configuration.enabled;
        probe.session_alarm_enabled = probe.default_configuration.alarm_enabled;
    }
}

void SmokerApplication::acquire_raw_inputs()
{
    chamber_temperature_ = chamber_sensor_.read();
    for (std::size_t index = 0U; index < probes_.size(); ++index) {
        auto& probe = probes_[index];
        probe.reading_sampled = false;
        if (probe.session_enabled) {
            probe.current_temperature = food_probe_source_.read(probe.default_configuration.id);
            probe.reading_sampled = true;
        } else {
            probe.current_temperature.reset();
        }
        probe_readings_[index].temperature = probe.current_temperature;
    }
}

void SmokerApplication::evaluate_probe_states(const core::MonotonicTimePoint now)
{
    for (auto& probe : probes_) {
        if (probe.session_enabled && probe.reading_sampled) {
            evaluate_probe_state(probe, now);
        }
    }
}

void SmokerApplication::evaluate_probe_state(
    ProbeRuntime& probe, const core::MonotonicTimePoint now
)
{
    const bool is_connected = probe.current_temperature.has_value();
    const auto probe_id = probe.default_configuration.id;
    if (!probe.reading_initialized) {
        emit(is_connected ? core::EventType::ProbeConnected : core::EventType::ProbeDisconnected,
             now,
             probe_id);
        probe.reading_initialized = true;
        probe.connected = is_connected;
    } else if (is_connected != probe.connected) {
        emit(is_connected ? core::EventType::ProbeReconnected : core::EventType::ProbeDisconnected,
             now,
             probe_id);
        probe.connected = is_connected;
    }

    if (is_connected) {
        probe.disconnected_alarm_emitted = false;
        resolve_probe_alarms(core::AlarmCode::ProbeDisconnected, probe_id);
    }

    if (!session_ || session_->status != core::SessionStatus::Running) {
        return;
    }

    if (!is_connected && probe.session_alarm_enabled
        && !probe.disconnected_alarm_emitted) {
        raise_alarm(core::AlarmCode::ProbeDisconnected, probe_id, now);
        probe.disconnected_alarm_emitted = true;
    }

    if (is_connected && probe.session_alarm_enabled
        && probe.session_target_temperature && !probe.target_alarm_emitted
        && *probe.current_temperature >= *probe.session_target_temperature) {
        raise_alarm(core::AlarmCode::ProbeTargetReached, probe_id, now);
        emit(core::EventType::ProbeTargetReached, now, probe_id);
        probe.target_alarm_emitted = true;
    }
}

void SmokerApplication::update_active_timer(const core::MonotonicTimePoint now)
{
    if (!session_ || session_->status != core::SessionStatus::Running
        || !session_->recipe_snapshot.stage.timer) {
        return;
    }

    const auto& timer_configuration = *session_->recipe_snapshot.stage.timer;
    const auto update = core::update_stage_timer(
        timer_configuration,
        session_->timer,
        now,
        chamber_temperature_,
        probe_readings_
    );

    if (update.started_now) {
        emit(core::EventType::TimerStarted, now);
    }
    if (!update.completed_now) {
        return;
    }

    emit(core::EventType::TimerCompleted, now);
    if (timer_configuration.completion_action == core::TimerCompletionAction::StopSession) {
        stop_running_session(core::StopReason::TimerCompleted, now);
    } else {
        raise_alarm(core::AlarmCode::TimerCompleted, std::nullopt, now);
    }
}

void SmokerApplication::evaluate_safety(const core::MonotonicTimePoint now)
{
    if (!configuration_valid_) {
        raise_fault(core::FaultCode::ConfigurationInvalid, now);
        return;
    }

    const auto evaluation = core::evaluate_chamber_safety(chamber_temperature_, safety_limits_);
    if (evaluation.fault_code) {
        raise_fault(*evaluation.fault_code, now);
    }
}

bool SmokerApplication::controller_is_eligible() const noexcept
{
    return configuration_valid_
        && !active_fault_
        && !controller_failure_pending_
        && !firmware_update_active_
        && session_
        && session_->status == core::SessionStatus::Running
        && chamber_temperature_.has_value()
        && session_->active_chamber_target.has_value();
}

bool SmokerApplication::reset_chamber_controller() noexcept
{
    return chamber_controller_.reset();
}

void SmokerApplication::raise_fault(
    const core::FaultCode code, const core::MonotonicTimePoint now
)
{
    if (active_fault_) {
        return;
    }

    active_fault_ = core::Fault{code, now, true};
    resolve_all_alarms();
    if (session_ && session_->status == core::SessionStatus::Running) {
        core::freeze_stage_timer(session_->timer, now);
        session_->status = core::SessionStatus::Fault;
        session_->stopped_at = now;
        session_->stop_reason = core::StopReason::Fault;
    }
    emit(core::EventType::FaultRaised, now, std::nullopt, std::nullopt, code);
}

void SmokerApplication::stop_running_session(
    const core::StopReason reason, const core::MonotonicTimePoint now
)
{
    if (!session_ || session_->status != core::SessionStatus::Running) {
        return;
    }

    core::freeze_stage_timer(session_->timer, now);
    if (session_->recipe_snapshot.stage.timer
        && session_->timer.elapsed > session_->recipe_snapshot.stage.timer->duration) {
        session_->timer.elapsed = session_->recipe_snapshot.stage.timer->duration;
    }
    session_->status = core::SessionStatus::Stopped;
    session_->stopped_at = now;
    session_->stop_reason = reason;
    resolve_all_alarms();
    emit(core::EventType::SessionStopped, now);
}

core::AlarmId SmokerApplication::raise_alarm(
    const core::AlarmCode code,
    const std::optional<core::ProbeId> probe_id,
    const core::MonotonicTimePoint now
)
{
    const auto reusable = std::find_if(
        alarms_.begin(), alarms_.end(),
        [code, probe_id](const core::Alarm& alarm) {
            return alarm.code == code && alarm.probe_id == probe_id;
        }
    );

    const core::Alarm alarm{next_alarm_id_++, code, probe_id, now, false, false};
    if (reusable != alarms_.end()) {
        *reusable = alarm;
    } else {
        alarms_.push_back(alarm);
    }
    return alarm.id;
}

void SmokerApplication::resolve_probe_alarms(
    const core::AlarmCode code, const core::ProbeId probe_id
) noexcept
{
    for (auto& alarm : alarms_) {
        if (alarm.code == code && alarm.probe_id == probe_id) {
            alarm.resolved = true;
        }
    }
}

void SmokerApplication::resolve_all_alarms() noexcept
{
    for (auto& alarm : alarms_) {
        alarm.resolved = true;
    }
}

void SmokerApplication::emit(
    const core::EventType type,
    const core::MonotonicTimePoint now,
    const std::optional<core::ProbeId> probe_id,
    const std::optional<core::AlarmId> alarm_id,
    const std::optional<core::FaultCode> fault_code
)
{
    pending_events_.push_back(core::Event{type, now, probe_id, alarm_id, fault_code});
}

void SmokerApplication::reject_command(const core::MonotonicTimePoint now)
{
    current_command_rejected_ = true;
    emit(core::EventType::CommandRejected, now);
}

void SmokerApplication::record_command_result(
    const std::uint32_t correlation_id, const bool accepted
) noexcept
{
    if (correlation_id == 0U) {
        return;
    }
    if (command_results_.size() == command_result_capacity) {
        std::move(
            command_results_.begin() + 1,
            command_results_.end(),
            command_results_.begin()
        );
        command_results_.back() = CommandResultView{correlation_id, accepted};
        return;
    }
    command_results_.push_back(CommandResultView{correlation_id, accepted});
}

void SmokerApplication::publish_pending_events() noexcept
{
    for (const auto& event : pending_events_) {
        event_sink_.publish(event);
    }
}

void SmokerApplication::refresh_snapshot_view_cache() noexcept
{
    probe_snapshot_views_.clear();
    for (const auto& probe : probes_) {
        probe_snapshot_views_.push_back(ProbeSnapshotView{
            probe.default_configuration.id,
            probe.default_configuration.name,
            probe.default_configuration.role,
            probe.current_temperature,
            probe.session_target_temperature,
            probe.session_enabled,
            probe.session_alarm_enabled,
        });
    }

    active_alarm_snapshot_.clear();
    for (const auto& alarm : alarms_) {
        if (!alarm.resolved) {
            active_alarm_snapshot_.push_back(alarm);
        }
    }
}

} // namespace smoker::app

#pragma once

#include "smoker/app/commands.hpp"
#include "smoker/app/ports.hpp"
#include "smoker/app/snapshot.hpp"
#include "smoker/app/snapshot_view.hpp"
#include "smoker/core/domain.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace smoker::app {

class SmokerApplication final {
public:
    SmokerApplication(
        IChamberSensor& chamber_sensor,
        IFoodProbeSource& food_probe_source,
        IChamberController& chamber_controller,
        IHeaterOutput& heater_output,
        IClock& clock,
        IEventSink& event_sink,
        core::SafetyLimits safety_limits,
        std::span<const core::FoodProbeConfig> probe_configuration
    );

    // M5 contract: the ControlTask is the sole caller. This method is not a
    // cross-task synchronization primitive. true means the command was admitted
    // to the queue, or that a consecutive trailing Stop was coalesced; it does
    // not mean the command has passed semantic validation. A Stop after any
    // intervening command is a distinct FIFO intent. Semantic rejection is
    // reported by CommandRejected during tick(). Stop has reserved admission;
    // regular-command overflow returns false, is counted, and is published on
    // the next tick.
    [[nodiscard]] bool submit(Command command, std::uint32_t correlation_id = 0U);
    void tick();
    [[nodiscard]] SmokerSnapshot snapshot() const;
    [[nodiscard]] SmokerSnapshotView snapshot_view() const noexcept;

private:
    static constexpr std::size_t command_capacity = 16U;
    static constexpr std::size_t regular_command_capacity = command_capacity - 1U;

    struct ProbeRuntime final {
        core::FoodProbeConfig default_configuration;
        std::optional<core::Temperature> session_target_temperature;
        bool session_enabled{false};
        bool session_alarm_enabled{false};
        std::optional<core::Temperature> current_temperature;
        bool reading_sampled{false};
        bool reading_initialized{false};
        bool connected{false};
        bool target_alarm_emitted{false};
        bool disconnected_alarm_emitted{false};
    };

    struct QueuedCommand final {
        Command command;
        std::uint32_t correlation_id{0U};
        std::array<std::uint32_t, command_result_capacity>
            coalesced_correlation_ids{};
        std::size_t coalesced_correlation_count{0U};
    };

    void process_pending_commands(core::MonotonicTimePoint now);
    void process(StartSessionCommand& command, core::MonotonicTimePoint now);
    void process(const StopSessionCommand& command, core::MonotonicTimePoint now);
    void process(const SetChamberTargetCommand& command, core::MonotonicTimePoint now);
    void process(const SetProbeTargetCommand& command, core::MonotonicTimePoint now);
    void process(const SetProbeEnabledCommand& command, core::MonotonicTimePoint now);
    void process(const SetProbeAlarmEnabledCommand& command, core::MonotonicTimePoint now);
    void process(const AcknowledgeAlarmCommand& command, core::MonotonicTimePoint now);
    void process(const ClearResolvedFaultCommand& command, core::MonotonicTimePoint now);
    void process(const PrepareFirmwareUpdateCommand& command, core::MonotonicTimePoint now);
    void process(const FinishFirmwareUpdateCommand& command, core::MonotonicTimePoint now);

    [[nodiscard]] bool recipe_is_valid(const core::Recipe& recipe) const noexcept;
    [[nodiscard]] bool probe_exists(core::ProbeId probe_id) const noexcept;
    [[nodiscard]] bool fault_condition_is_resolved(core::FaultCode code) const noexcept;
    [[nodiscard]] core::SessionStatus current_session_status() const noexcept;
    [[nodiscard]] ProbeRuntime* find_probe(core::ProbeId probe_id) noexcept;

    [[nodiscard]] bool trailing_stop_is_pending() const noexcept;
    void reset_probe_session_configuration() noexcept;
    void acquire_raw_inputs();
    void evaluate_probe_states(core::MonotonicTimePoint now);
    void evaluate_probe_state(ProbeRuntime& probe, core::MonotonicTimePoint now);
    void update_active_timer(core::MonotonicTimePoint now);
    void evaluate_safety(core::MonotonicTimePoint now);
    [[nodiscard]] bool controller_is_eligible() const noexcept;
    [[nodiscard]] bool reset_chamber_controller() noexcept;
    void raise_fault(core::FaultCode code, core::MonotonicTimePoint now);
    void stop_running_session(core::StopReason reason, core::MonotonicTimePoint now);
    core::AlarmId raise_alarm(
        core::AlarmCode code,
        std::optional<core::ProbeId> probe_id,
        core::MonotonicTimePoint now
    );
    void resolve_probe_alarms(core::AlarmCode code, core::ProbeId probe_id) noexcept;
    void resolve_all_alarms() noexcept;
    void emit(
        core::EventType type,
        core::MonotonicTimePoint now,
        std::optional<core::ProbeId> probe_id = std::nullopt,
        std::optional<core::AlarmId> alarm_id = std::nullopt,
        std::optional<core::FaultCode> fault_code = std::nullopt
    );
    void reject_command(core::MonotonicTimePoint now);
    void record_command_result(std::uint32_t correlation_id, bool accepted) noexcept;
    void publish_pending_events() noexcept;
    void refresh_snapshot_view_cache() noexcept;

    IChamberSensor& chamber_sensor_;
    IFoodProbeSource& food_probe_source_;
    IChamberController& chamber_controller_;
    IHeaterOutput& heater_output_;
    IClock& clock_;
    IEventSink& event_sink_;
    core::SafetyLimits safety_limits_;

    std::optional<core::Session> session_;
    std::optional<core::Temperature> chamber_temperature_;
    core::HeaterDemand heater_demand_{core::HeaterDemand::off()};
    std::optional<core::Fault> active_fault_;
    std::vector<ProbeRuntime> probes_;
    std::vector<core::ProbeReading> probe_readings_;
    std::vector<core::Alarm> alarms_;
    std::vector<ProbeSnapshotView> probe_snapshot_views_;
    std::vector<core::Alarm> active_alarm_snapshot_;
    std::vector<core::Event> pending_events_;
    std::vector<CommandResultView> command_results_;
    core::AlarmId next_alarm_id_{1U};
    bool configuration_valid_{true};
    bool firmware_update_active_{false};
    bool controller_active_{false};
    bool controller_fault_resolved_{false};
    bool controller_failure_pending_{false};
    core::Duration session_elapsed_{};

    std::array<std::optional<QueuedCommand>, command_capacity> commands_{};
    std::size_t command_head_{0U};
    std::size_t command_tail_{0U};
    std::size_t command_count_{0U};
    std::size_t command_queue_overflow_count_{0U};
    std::size_t reported_command_queue_overflow_count_{0U};
    bool current_command_rejected_{false};
};

} // namespace smoker::app

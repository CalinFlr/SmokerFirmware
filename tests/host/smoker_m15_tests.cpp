#include "smoker/platform/blynk_connection_support.hpp"
#include "smoker/platform/blynk_command_support.hpp"
#include "smoker/platform/blynk_provisioning_support.hpp"
#include "smoker/platform/blynk_remote_support.hpp"
#include "smoker/platform/runtime_transport_support.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include "smoker/app/smoker_application.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace allocation_probe {

std::atomic_bool enabled{false};
std::atomic_size_t allocations{0U};

void begin() noexcept
{
    allocations.store(0U, std::memory_order_relaxed);
    enabled.store(true, std::memory_order_relaxed);
}

std::size_t end() noexcept
{
    enabled.store(false, std::memory_order_relaxed);
    return allocations.load(std::memory_order_relaxed);
}

} // namespace allocation_probe

void* operator new(const std::size_t size)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed)) {
        allocation_probe::allocations.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void* const memory = std::malloc(size)) return memory;
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
    ::operator delete[](memory);
}

namespace {

using smoker::platform::BlynkRemoteProjection;
using smoker::platform::BlynkRemoteStatus;

template <typename Mapper>
concept HasDisconnectCleanup = requires(Mapper& mapper) {
    mapper.disconnected();
};

static_assert(!HasDisconnectCleanup<smoker::platform::BlynkCommandMapper>);

smoker::core::Temperature temperature(const float value)
{
    const auto result = smoker::core::Temperature::from_celsius(value);
    assert(result.has_value());
    return *result;
}

smoker::core::Recipe recipe()
{
    return {
        1U,
        "remote",
        smoker::core::Stage{1U, "stage", temperature(110.0F), std::nullopt},
    };
}

void copy_string(auto& destination, const std::string_view value)
{
    assert(value.size() < destination.size());
    std::copy(value.begin(), value.end(), destination.begin());
    destination[value.size()] = '\0';
}

void test_projection_connect_throttle_coalescing_and_retry()
{
    BlynkRemoteProjection projection;
    BlynkRemoteStatus first{};
    first.heater_percent = 10;
    projection.observe(first);
    assert(!projection.pending_publish(0).has_value());
    projection.connected();
    assert(projection.pending_publish(0) == first);
    // A failed network attempt does not commit or clear the candidate.
    assert(projection.pending_publish(1) == first);
    projection.publish_succeeded(1);
    assert(!projection.dirty());
    assert(!projection.pending_publish(60'000).has_value());

    auto changed = first;
    changed.heater_percent = 20;
    projection.observe(changed);
    assert(!projection.pending_publish(5'000).has_value());
    auto newest = changed;
    newest.heater_percent = 30;
    projection.observe(newest);
    assert(projection.pending_publish(5'001) == newest);
    projection.publish_succeeded(5'001);

    // A transient change that returns to the last published projection is
    // coalesced away instead of creating a duplicate status message.
    projection.observe(changed);
    projection.observe(newest);
    assert(!projection.dirty());
    assert(!projection.pending_publish(100'000).has_value());

    projection.disconnected();
    projection.connected();
    assert(projection.pending_publish(100'000) == newest);
}

void test_status_timer_normalization_and_serializer_budget()
{
    smoker::app::ProbeSnapshotView probe{
        1U, "Probe \"one\"", smoker::core::ProbeRole::Meat,
        temperature(70.04F), temperature(75.06F), true, true,
    };
    smoker::app::SmokerSnapshotView snapshot{
        smoker::core::SessionStatus::Running,
        9U,
        smoker::core::Duration{12'345},
        smoker::core::StopReason::None,
        temperature(99.96F),
        temperature(110.04F),
        *smoker::core::HeaterDemand::from_percent(49.6F),
        false,
        {},
        std::span<const smoker::app::ProbeSnapshotView>{&probe, 1U},
        {},
        std::nullopt,
        false,
        temperature(150.0F),
        0U,
        {},
    };
    smoker::platform::FirmwareUpdateStatus firmware{};
    copy_string(firmware.current_version, "0.16.0");
    auto status = smoker::platform::make_blynk_remote_status(snapshot, firmware);
    assert(std::string_view{status.timer_state.data()} == "NONE");
    assert(std::string_view{status.firmware_available_version.data()}.empty());
    assert(status.timer_elapsed_seconds == -1);
    assert(status.chamber_current_deci_celsius == 1000);
    assert(status.heater_percent == 50);
    assert(std::string_view{status.probe_summary.data()}.find('"')
        == std::string_view::npos);

    firmware.state = smoker::platform::FirmwareUpdateState::UpToDate;
    status = smoker::platform::make_blynk_remote_status(snapshot, firmware);
    assert(std::string_view{status.firmware_available_version.data()} == "Latest");

    firmware.state = smoker::platform::FirmwareUpdateState::Available;
    copy_string(firmware.available_version, "v0.16.0");
    status = smoker::platform::make_blynk_remote_status(snapshot, firmware);
    assert(std::string_view{status.firmware_available_version.data()} == "v0.16.0");

    snapshot.timer_configured = true;
    status = smoker::platform::make_blynk_remote_status(snapshot, firmware);
    assert(std::string_view{status.timer_state.data()} == "WAITING");
    assert(status.timer_elapsed_seconds == 0);

    // Maximize every bounded text field and verify deterministic failure-free
    // serialization remains below both the chosen payload budget and Blynk's
    // public 1,024-byte message limit.
    std::fill(status.probe_summary.begin(), status.probe_summary.end() - 1, 'P');
    std::fill(status.firmware_error.begin(), status.firmware_error.end() - 1, 'E');
    std::fill(status.firmware_current_version.begin(), status.firmware_current_version.end() - 1, '1');
    std::fill(status.firmware_available_version.begin(), status.firmware_available_version.end() - 1, '2');
    std::fill(status.fault_code.begin(), status.fault_code.end() - 1, 'F');
    const auto payload = smoker::platform::serialize_blynk_batch(status);
    assert(payload.has_value());
    assert(payload->length <= smoker::platform::blynk_payload_capacity);
    assert(payload->length < smoker::platform::blynk_public_message_limit);
    assert(payload->view().starts_with("{"));
    assert(payload->view().ends_with("}"));
    assert(payload->view().find("LastCommandResult") == std::string_view::npos);

    snapshot.chamber_temperature = temperature(-0.5F);
    probe.current_temperature = temperature(-0.5F);
    status = smoker::platform::make_blynk_remote_status(snapshot, firmware);
    const auto negative_payload = smoker::platform::serialize_blynk_batch(status);
    assert(negative_payload);
    assert(negative_payload->view().find("\"ChamberCurrentC\":-0.5")
        != std::string_view::npos);
    assert(std::string_view{status.probe_summary.data()}.find(":-0.5:")
        != std::string_view::npos);
}

void test_allowlisted_deterministic_command_mapping()
{
    smoker::platform::BlynkCommandMapper mapper{recipe()};
    const auto stop = mapper.map("CmdStop", "1", 1U);
    const auto chamber = mapper.map("CmdChamberTargetC", "-273.1", 1U);
    const auto probe_target = mapper.map("CmdProbeTarget", "255,75.125", 1U);
    const auto probe_enabled = mapper.map("CmdProbeEnabled", "1,0", 1U);
    const auto probe_alarm = mapper.map("CmdProbeAlarmEnabled", "1,1", 1U);
    const auto acknowledge = mapper.map("CmdAcknowledgeAlarm", "4294967295", 1U);
    const auto clear_fault = mapper.map("CmdClearResolvedFault", "1", 1U);
    assert(std::get_if<smoker::app::StopSessionCommand>(&*stop.command) != nullptr);
    assert(std::get_if<smoker::app::SetChamberTargetCommand>(&*chamber.command)->target
        == std::nullopt);
    assert(std::get_if<smoker::app::SetProbeTargetCommand>(&*probe_target.command) != nullptr);
    assert(std::get_if<smoker::app::SetProbeEnabledCommand>(&*probe_enabled.command) != nullptr);
    assert(std::get_if<smoker::app::SetProbeAlarmEnabledCommand>(&*probe_alarm.command) != nullptr);
    assert(std::get_if<smoker::app::AcknowledgeAlarmCommand>(&*acknowledge.command) != nullptr);
    assert(std::get_if<smoker::app::ClearResolvedFaultCommand>(&*clear_fault.command) != nullptr);
    assert(mapper.map("CmdFirmware", "1", 1U).firmware_operation
        == smoker::platform::BlynkFirmwareOperation::Check);
    assert(mapper.map("CmdFirmware", "2", 1U).firmware_operation
        == smoker::platform::BlynkFirmwareOperation::Install);

    constexpr std::array<std::string_view, 10U> invalid_temperatures{
        "", "+1", " 1", "1 ", "1e2", ".5", "5.", "1.0000", "--1", "nan",
    };
    for (const auto value : invalid_temperatures) {
        assert(mapper.map("CmdChamberTargetC", value, 1U).decision
            == smoker::platform::BlynkCommandDecision::Malformed);
    }
    assert(mapper.map("CmdProbeTarget", "256,75", 1U).decision
        == smoker::platform::BlynkCommandDecision::Malformed);
    assert(mapper.map("CmdProbeEnabled", "1,2", 1U).decision
        == smoker::platform::BlynkCommandDecision::Malformed);
    assert(mapper.map("CmdStop", "0", 1U).decision
        == smoker::platform::BlynkCommandDecision::Ignored);
    assert(mapper.map("Unknown", "1", 1U).decision
        == smoker::platform::BlynkCommandDecision::Ignored);
}

void test_atomic_start_mapping_and_strict_parser()
{
    smoker::platform::BlynkCommandMapper mapper{recipe()};

    const auto implicit = mapper.map("CmdStartRequest", "1", 44U);
    const auto* const implicit_start = std::get_if<smoker::app::StartSessionCommand>(
        &*implicit.command
    );
    assert(implicit.decision == smoker::platform::BlynkCommandDecision::Accepted);
    assert(implicit_start != nullptr && implicit_start->session_id == 44U);
    assert(implicit_start->recipe.stage.chamber_target->celsius() == 110.0F);

    const auto explicit_target = mapper.map("CmdStartRequest", "1,125.0", 45U);
    const auto* const explicit_start = std::get_if<smoker::app::StartSessionCommand>(
        &*explicit_target.command
    );
    assert(explicit_target.decision
        == smoker::platform::BlynkCommandDecision::Accepted);
    assert(explicit_start != nullptr && explicit_start->session_id == 45U);
    assert(explicit_start->recipe.stage.chamber_target->celsius() == 125.0F);

    const auto monitoring = mapper.map("CmdStartRequest", "1,-273.1", 46U);
    const auto* const monitoring_start = std::get_if<smoker::app::StartSessionCommand>(
        &*monitoring.command
    );
    assert(monitoring.decision == smoker::platform::BlynkCommandDecision::Accepted);
    assert(monitoring_start != nullptr && monitoring_start->session_id == 46U);
    assert(!monitoring_start->recipe.stage.chamber_target.has_value());

    // Each request is complete in itself: neither explicit form can influence
    // the following startup-recipe form.
    const auto independent = mapper.map("CmdStartRequest", "1", 47U);
    const auto* const independent_start =
        std::get_if<smoker::app::StartSessionCommand>(&*independent.command);
    assert(independent_start != nullptr);
    assert(independent_start->recipe.stage.chamber_target->celsius() == 110.0F);

    constexpr std::array<std::string_view, 10U> malformed_requests{
        "", "2", "1,", ",110", "1,110,120", "1, 110", "1,+110",
        "1,1e2", "start,110", "1,nan",
    };
    for (const auto payload : malformed_requests) {
        allocation_probe::begin();
        const auto mapped = mapper.map("CmdStartRequest", payload, 99U);
        const auto allocations = allocation_probe::end();
        assert(mapped.decision == smoker::platform::BlynkCommandDecision::Malformed);
        assert(!mapped.command.has_value());
        assert(allocations == 0U);
    }
}

void test_atomic_start_release_is_noop_without_feedback()
{
    smoker::platform::BlynkCommandMapper mapper{recipe()};
    smoker::app::SpscCommandMailbox application_mailbox;
    smoker::platform::BlynkCommandResults results;
    smoker::platform::BlynkEventScheduler events;

    const auto release = mapper.map("CmdStartRequest", "0", 60U);
    assert(release.decision == smoker::platform::BlynkCommandDecision::Ignored);
    assert(!release.command.has_value());
    assert(release.firmware_operation
        == smoker::platform::BlynkFirmwareOperation::None);
    const auto protocol_error = smoker::platform::blynk_command_error_message(
        release.decision
    );
    assert(protocol_error.empty());

    // Mirror the service decision gates: only non-empty protocol errors emit a
    // remote event, and only Accepted mappings enter correlation tracking.
    if (!protocol_error.empty()) {
        events.queue(smoker::platform::BlynkEventType::RemoteError, protocol_error);
    }
    if (release.decision == smoker::platform::BlynkCommandDecision::Accepted
        && release.command.has_value()) {
        constexpr std::uint32_t correlation = 600U;
        assert(application_mailbox.push(*release.command, correlation, 1U)
            == smoker::app::MailboxAdmission::Accepted);
        assert(results.track(correlation));
    }

    assert(application_mailbox.pending() == 0U);
    assert(results.pending_count() == 0U);
    assert(!events.pending_publish(0).has_value());
}

void test_atomic_start_button_press_release_sequences()
{
    const auto exercise = [](const std::string_view press_payload,
                             const float expected_target) {
        smoker::platform::BlynkCommandMapper mapper{recipe()};
        smoker::app::SpscCommandMailbox application_mailbox;
        smoker::platform::BlynkCommandResults results;
        smoker::platform::BlynkEventScheduler events;
        std::size_t accepted_commands = 0U;
        std::uint32_t next_correlation = 800U;

        const std::array payloads{press_payload, std::string_view{"0"}};
        for (std::size_t index = 0U; index < payloads.size(); ++index) {
            const auto mapped = mapper.map(
                "CmdStartRequest", payloads[index],
                static_cast<smoker::core::SessionId>(80U + index)
            );
            const auto protocol_error =
                smoker::platform::blynk_command_error_message(mapped.decision);
            if (!protocol_error.empty()) {
                events.queue(
                    smoker::platform::BlynkEventType::RemoteError, protocol_error
                );
                continue;
            }
            if (mapped.decision != smoker::platform::BlynkCommandDecision::Accepted) {
                assert(!mapped.command.has_value());
                continue;
            }
            assert(mapped.command.has_value());
            const auto correlation = next_correlation++;
            assert(application_mailbox.push(*mapped.command, correlation, 4U)
                == smoker::app::MailboxAdmission::Accepted);
            assert(results.track(correlation));
            ++accepted_commands;
        }

        assert(accepted_commands == 1U);
        assert(application_mailbox.pending() == 1U);
        assert(results.pending_count() == 1U);
        assert(!events.pending_publish(0).has_value());

        smoker::app::Command admitted;
        assert(application_mailbox.try_pop(admitted));
        assert(application_mailbox.pending() == 0U);
        const auto* const start =
            std::get_if<smoker::app::StartSessionCommand>(&admitted);
        assert(start != nullptr);
        assert(start->recipe.stage.chamber_target.has_value());
        assert(start->recipe.stage.chamber_target->celsius() == expected_target);
    };

    exercise("1", 110.0F);
    exercise("1,125.0", 125.0F);
}

void test_legacy_start_protocol_fails_closed()
{
    smoker::platform::BlynkCommandMapper mapper{recipe()};
    const auto legacy_target = mapper.map("CmdStartTargetC", "125.0", 1U);
    const auto legacy_start = mapper.map("CmdStart", "1", 2U);
    assert(legacy_target.decision
        == smoker::platform::BlynkCommandDecision::Deprecated);
    assert(legacy_start.decision
        == smoker::platform::BlynkCommandDecision::Deprecated);
    assert(!legacy_target.command.has_value() && !legacy_start.command.has_value());

    const auto release = mapper.map("CmdStartRequest", "0", 3U);
    assert(release.decision == smoker::platform::BlynkCommandDecision::Ignored);
    assert(!release.command.has_value());
    assert(smoker::platform::blynk_command_error_message(release.decision).empty());

    const auto atomic = mapper.map("CmdStartRequest", "1", 48U);
    const auto* const atomic_start =
        std::get_if<smoker::app::StartSessionCommand>(&*atomic.command);
    assert(atomic_start != nullptr && atomic_start->session_id == 48U);
    assert(atomic_start->recipe.stage.chamber_target->celsius() == 110.0F);

    const auto explicit_atomic = mapper.map("CmdStartRequest", "1,130.0", 49U);
    const auto* const explicit_start =
        std::get_if<smoker::app::StartSessionCommand>(&*explicit_atomic.command);
    assert(explicit_start != nullptr);
    assert(explicit_start->recipe.stage.chamber_target->celsius() == 130.0F);
}

void test_atomic_start_feedback_and_correlation()
{
    smoker::platform::BlynkCommandMapper mapper{recipe()};
    smoker::platform::BlynkEventScheduler events;
    smoker::platform::BlynkCommandResults results;

    const auto malformed = mapper.map("CmdStartRequest", "1,+110", 70U);
    const auto deprecated = mapper.map("CmdStart", "1", 71U);
    assert(!malformed.command.has_value() && !deprecated.command.has_value());
    const auto malformed_message = smoker::platform::blynk_command_error_message(
        malformed.decision
    );
    const auto deprecated_message = smoker::platform::blynk_command_error_message(
        deprecated.decision
    );
    assert(malformed_message == "malformed remote command");
    assert(deprecated_message == "deprecated remote start protocol");
    assert(malformed_message.size() < 160U && deprecated_message.size() < 160U);
    events.queue(smoker::platform::BlynkEventType::RemoteError, malformed_message);
    events.queue(smoker::platform::BlynkEventType::RemoteError, deprecated_message);
    const auto remote_error = events.pending_publish(0);
    assert(remote_error.has_value());
    assert(std::string_view{remote_error->description.data()}
        == "deprecated remote start protocol");
    assert(results.pending_count() == 0U);

    const auto mapped = mapper.map("CmdStartRequest", "1,125.0", 72U);
    assert(mapped.decision == smoker::platform::BlynkCommandDecision::Accepted);
    smoker::app::SpscCommandMailbox application_mailbox;
    constexpr std::uint32_t correlation = 700U;
    constexpr std::uint32_t generation = 9U;
    assert(application_mailbox.push(*mapped.command, correlation, generation)
        == smoker::app::MailboxAdmission::Accepted);
    assert(results.track(correlation));

    smoker::app::Command admitted;
    std::uint32_t admitted_correlation = 0U;
    std::uint32_t admitted_generation = 0U;
    assert(application_mailbox.try_pop(
        admitted, &admitted_correlation, &admitted_generation
    ));
    assert(std::get_if<smoker::app::StartSessionCommand>(&admitted) != nullptr);
    assert(admitted_correlation == correlation && admitted_generation == generation);
    const std::array semantic_result{
        smoker::app::CommandResultView{correlation, true},
    };
    results.observe(semantic_result);
    const auto feedback = results.pop();
    assert(feedback.has_value());
    assert(feedback->kind == smoker::platform::BlynkCommandResultKind::SemanticAccepted);
    assert(smoker::platform::serialize_blynk_command_feedback(*feedback).view()
        == "700:accepted");
}

void test_raw_mailbox_stop_reservation_and_concurrency()
{
    smoker::platform::BlynkInboundMailbox mailbox;
    for (std::size_t index = 0U; index < smoker::platform::blynk_inbound_capacity - 1U; ++index) {
        assert(mailbox.push("CmdAcknowledgeAlarm", "1")
            == smoker::platform::BlynkInboundAdmission::Accepted);
    }
    assert(mailbox.push("CmdStartRequest", "1")
        == smoker::platform::BlynkInboundAdmission::Full);
    assert(mailbox.push("CmdStop", "1")
        == smoker::platform::BlynkInboundAdmission::Accepted);
    assert(mailbox.push("Unknown", "1")
        == smoker::platform::BlynkInboundAdmission::Ignored);
    assert(mailbox.push("CmdStop", std::string(80U, '1'))
        == smoker::platform::BlynkInboundAdmission::Malformed);

    smoker::platform::BlynkInboundCommand command;
    std::size_t count = 0U;
    while (mailbox.try_pop(command)) ++count;
    assert(count == smoker::platform::blynk_inbound_capacity);

    smoker::platform::BlynkInboundMailbox allowlist;
    assert(allowlist.push("CmdStartRequest", "1,110.0")
        == smoker::platform::BlynkInboundAdmission::Accepted);
    assert(allowlist.push("CmdStart", "1")
        == smoker::platform::BlynkInboundAdmission::Accepted);
    assert(allowlist.push("CmdStartTargetC", "110.0")
        == smoker::platform::BlynkInboundAdmission::Accepted);
    assert(allowlist.push("ArbitraryTopic", "1")
        == smoker::platform::BlynkInboundAdmission::Ignored);

    smoker::platform::BlynkInboundMailbox concurrent;
    std::atomic_bool ordered{true};
    std::thread producer{[&concurrent]() {
        for (std::uint32_t id = 1U; id <= 2'000U; ++id) {
            const auto payload = std::to_string(id);
            while (concurrent.push("CmdAcknowledgeAlarm", payload)
                   == smoker::platform::BlynkInboundAdmission::Full) {
                std::this_thread::yield();
            }
        }
    }};
    std::thread consumer{[&concurrent, &ordered]() {
        smoker::platform::BlynkInboundCommand value;
        for (std::uint32_t expected = 1U; expected <= 2'000U; ++expected) {
            while (!concurrent.try_pop(value)) std::this_thread::yield();
            if (value.payload_view() != std::to_string(expected)) ordered.store(false);
        }
    }};
    producer.join();
    consumer.join();
    assert(ordered.load() && concurrent.pending() == 0U);
}

void test_disconnect_reconnect_boundary_discards_old_connection_state()
{
    smoker::platform::BlynkConnectionBoundary boundary;
    const auto first_generation = boundary.callback_connected();
    const auto first_connection = boundary.poll();
    assert(first_connection.connected && first_connection.connection_started);
    assert(!first_connection.cleanup_required);
    assert(boundary.usable(first_connection));

    smoker::platform::BlynkInboundMailbox inbound;
    assert(inbound.push("CmdStartRequest", "1,130.0", first_generation)
        == smoker::platform::BlynkInboundAdmission::Accepted);

    smoker::platform::BlynkCommandMapper mapper{recipe()};
    smoker::platform::BlynkCommandResults results;
    assert(results.track(41U));
    const std::array old_result{smoker::app::CommandResultView{41U, true}};
    results.observe(old_result);
    smoker::platform::BlynkEventScheduler events;
    events.queue(smoker::platform::BlynkEventType::Alarm, "old alarm");

    smoker::platform::BlynkInboundMailbox saturated;
    for (std::size_t index = 0U;
         index < smoker::platform::blynk_inbound_capacity - 1U; ++index) {
        assert(saturated.push("CmdAcknowledgeAlarm", "1", first_generation)
            == smoker::platform::BlynkInboundAdmission::Accepted);
    }
    assert(saturated.push("CmdStartRequest", "1", first_generation)
        == smoker::platform::BlynkInboundAdmission::Full);
    std::uint32_t observed_drops = 0U;

    boundary.callback_disconnected();
    const auto second_generation = boundary.callback_connected();
    // A genuinely new command can already be queued before the consumer sees
    // the collapsed disconnect/reconnect pair.
    assert(inbound.push("CmdStartRequest", "1", second_generation)
        == smoker::platform::BlynkInboundAdmission::Accepted);

    const auto reconnected = boundary.poll();
    assert(reconnected.connected && reconnected.cleanup_required);
    assert(reconnected.connection_started);
    assert(reconnected.connection_generation == second_generation);
    assert(!boundary.accepts(first_generation));
    assert(boundary.accepts(second_generation));

    // These are the platform-neutral parts of handle_disconnect(). The ESP-IDF
    // integration is guarded separately because blynk_service.cpp is target-only.
    results.disconnected();
    events.disconnected();
    observed_drops = saturated.dropped_count();
    assert(!results.pop().has_value() && results.pending_count() == 0U);
    assert(!events.pending_publish(100'000).has_value());
    assert(observed_drops == saturated.dropped_count());
    assert(saturated.push("CmdStartRequest", "1", second_generation)
        == smoker::platform::BlynkInboundAdmission::Full);
    // Only the old-generation watermark was acknowledged. A real drop from
    // the new generation remains distinguishable and reportable.
    assert(observed_drops != saturated.dropped_count());

    smoker::platform::BlynkInboundCommand command;
    std::size_t discarded = 0U;
    std::size_t accepted = 0U;
    while (inbound.try_pop(command)) {
        if (command.connection_generation != reconnected.connection_generation) {
            ++discarded;
            continue;
        }
        const auto mapped = mapper.map(
            command.datastream_view(), command.payload_view(), 52U
        );
        assert(mapped.decision == smoker::platform::BlynkCommandDecision::Accepted);
        const auto* const start = std::get_if<smoker::app::StartSessionCommand>(
            &*mapped.command
        );
        assert(start != nullptr);
        // The old atomic request is discarded as a unit; the new live request
        // independently uses the startup recipe.
        assert(start->recipe.stage.chamber_target->celsius() == 110.0F);
        ++accepted;
    }
    assert(discarded == 1U && accepted == 1U);
}

void test_control_is_independent_of_blynk_transport()
{
    const std::array probes{
        smoker::core::FoodProbeConfig{
            1U, "Probe", smoker::core::ProbeRole::Meat,
            temperature(75.0F), true, true,
        },
    };
    smoker::platform::SimulatedChamberSensor chamber{temperature(25.0F)};
    smoker::platform::SimulatedFoodProbeSource probe_source{probes};
    smoker::platform::DeterministicChamberController chamber_controller;
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    smoker::app::SmokerApplication application{
        chamber, probe_source, chamber_controller, heater, clock, events,
        smoker::core::SafetyLimits{temperature(150.0F)}, probes,
    };
    smoker::platform::BlynkInboundMailbox stalled_transport;
    for (std::size_t index = 0U;
         index < smoker::platform::blynk_inbound_capacity - 1U; ++index) {
        assert(stalled_transport.push("CmdAcknowledgeAlarm", "1")
            == smoker::platform::BlynkInboundAdmission::Accepted);
    }
    assert(application.submit(smoker::app::StartSessionCommand{7U, recipe()}));
    application.tick();
    assert(heater.last_demand().percent() == 100.0F);
    assert(stalled_transport.pending()
        == smoker::platform::blynk_inbound_capacity - 1U);

    chamber.set_reading(std::nullopt);
    clock.advance(smoker::core::Duration{1'000});
    application.tick();
    assert(heater.last_demand().percent() == 0.0F);
    assert(application.snapshot_view().session_status
        == smoker::core::SessionStatus::Fault);
}

struct DrainCapture final {
    std::vector<std::uint32_t> correlations;
    std::vector<bool> stops;
};

bool capture_submission(
    void* const context,
    smoker::app::Command command,
    const std::uint32_t correlation
) noexcept
{
    auto& capture = *static_cast<DrainCapture*>(context);
    capture.correlations.push_back(correlation);
    capture.stops.push_back(std::holds_alternative<smoker::app::StopSessionCommand>(command));
    return true;
}

bool validate_boundary_generation(
    const void* const context,
    const std::uint32_t connection_generation
) noexcept
{
    return static_cast<const smoker::platform::BlynkConnectionBoundary*>(context)
        ->accepts(connection_generation);
}

void test_translated_commands_do_not_cross_reconnect_boundary()
{
    smoker::platform::BlynkConnectionBoundary boundary;
    const auto first_generation = boundary.callback_connected();
    static_cast<void>(boundary.poll());

    smoker::app::SpscCommandMailbox http;
    smoker::app::SpscCommandMailbox blynk;
    assert(blynk.push(
        smoker::app::StartSessionCommand{61U, recipe()}, 601U, first_generation
    ) == smoker::app::MailboxAdmission::Accepted);

    boundary.callback_disconnected();
    const auto second_generation = boundary.callback_connected();
    const auto reconnected = boundary.poll();
    assert(reconnected.cleanup_required && reconnected.connection_started);

    smoker::platform::RoundRobinCommandDrain drain;
    DrainCapture capture;
    const auto stale = drain.drain(
        http, blynk, &capture, capture_submission,
        &boundary, validate_boundary_generation
    );
    assert(stale.submitted == 0U && stale.discarded == 1U);
    assert(capture.correlations.empty());

    assert(blynk.push(
        smoker::app::AcknowledgeAlarmCommand{7U}, 602U, second_generation
    ) == smoker::app::MailboxAdmission::Accepted);
    const auto live = drain.drain(
        http, blynk, &capture, capture_submission,
        &boundary, validate_boundary_generation
    );
    assert(live.submitted == 1U && live.discarded == 0U);
    assert(capture.correlations == std::vector<std::uint32_t>{602U});
}

void test_shared_ids_wrap_concurrency_and_fair_drain()
{
    smoker::platform::RuntimeIdGenerator wrapped{
        smoker::platform::internal_ota_correlation_id,
        std::numeric_limits<std::uint32_t>::max(),
    };
    assert(wrapped.next_session() == std::numeric_limits<std::uint32_t>::max());
    assert(wrapped.next_session() == 1U);
    assert(wrapped.next_correlation() == std::numeric_limits<std::uint32_t>::max());
    assert(wrapped.next_correlation() == 1U);

    smoker::platform::RuntimeIdGenerator concurrent;
    std::array<std::uint32_t, 2'000U> values{};
    std::thread first{[&]() {
        for (std::size_t index = 0U; index < 1'000U; ++index) {
            values[index] = concurrent.next_correlation();
        }
    }};
    std::thread second{[&]() {
        for (std::size_t index = 1'000U; index < values.size(); ++index) {
            values[index] = concurrent.next_correlation();
        }
    }};
    first.join();
    second.join();
    std::sort(values.begin(), values.end());
    assert(std::adjacent_find(values.begin(), values.end()) == values.end());
    assert(values.front() != 0U);
    assert(std::find(values.begin(), values.end(), smoker::platform::internal_ota_correlation_id)
        == values.end());

    smoker::app::SpscCommandMailbox http;
    smoker::app::SpscCommandMailbox blynk;
    for (std::uint32_t id = 100U; id < 115U; ++id) {
        assert(http.push(smoker::app::AcknowledgeAlarmCommand{id}, id)
            == smoker::app::MailboxAdmission::Accepted);
    }
    for (std::uint32_t id = 200U; id < 212U; ++id) {
        assert(blynk.push(smoker::app::AcknowledgeAlarmCommand{id}, id)
            == smoker::app::MailboxAdmission::Accepted);
    }
    smoker::platform::RoundRobinCommandDrain drain;
    DrainCapture capture;
    const auto result = drain.drain(http, blynk, &capture, capture_submission);
    assert(result.submitted == smoker::platform::RoundRobinCommandDrain::external_budget_per_cycle);
    assert(http.pending() != 0U && blynk.pending() != 0U);
    for (std::size_t index = 1U; index < capture.correlations.size(); ++index) {
        assert((capture.correlations[index - 1U] < 200U)
            != (capture.correlations[index] < 200U));
    }

    smoker::app::SpscCommandMailbox stop_http;
    smoker::app::SpscCommandMailbox stop_blynk;
    assert(stop_http.push(smoker::app::StopSessionCommand{}, 7U)
        == smoker::app::MailboxAdmission::Accepted);
    assert(stop_blynk.push(smoker::app::AcknowledgeAlarmCommand{8U}, 8U)
        == smoker::app::MailboxAdmission::Accepted);
    DrainCapture stopped;
    const auto stop_result = drain.drain(
        stop_http, stop_blynk, &stopped, capture_submission
    );
    assert(stop_result.stopped_at_barrier);
    assert(stopped.stops.back());
}

void test_results_and_events_are_separate_and_not_replayed()
{
    smoker::platform::BlynkCommandResults results;
    assert(results.track(10U));
    const std::array<smoker::app::CommandResultView, 2U> shared{
        smoker::app::CommandResultView{9U, true},
        smoker::app::CommandResultView{10U, false},
    };
    results.observe(shared);
    const auto feedback = results.pop();
    assert(feedback && feedback->correlation_id == 10U);
    assert(feedback->kind == smoker::platform::BlynkCommandResultKind::SemanticRejected);
    assert(smoker::platform::serialize_blynk_command_feedback(*feedback).view()
        == "10:rejected");
    assert(results.track(11U));
    assert(results.record_service_result(12U, true));
    results.disconnected();
    results.observe({shared.data(), shared.size()});
    assert(!results.pop().has_value() && results.pending_count() == 0U);

    smoker::platform::BlynkCommandResults backed_up_results;
    for (std::uint32_t id = 1U;
         id < smoker::platform::BlynkCommandResults::capacity; ++id) {
        assert(backed_up_results.record_service_result(id, true));
    }
    assert(backed_up_results.track(100U));
    const std::array semantic{smoker::app::CommandResultView{100U, true}};
    backed_up_results.observe(semantic);
    assert(backed_up_results.pending_count() == 0U);
    assert(!backed_up_results.track(101U));
    const std::array overwritten_snapshot{
        smoker::app::CommandResultView{101U, false}
    };
    backed_up_results.observe(overwritten_snapshot);
    bool found_semantic_feedback = false;
    for (std::size_t index = 0U;
         index < smoker::platform::BlynkCommandResults::capacity; ++index) {
        const auto queued = backed_up_results.pop();
        assert(queued);
        if (queued->correlation_id == 100U) {
            found_semantic_feedback = true;
            assert(queued->kind
                == smoker::platform::BlynkCommandResultKind::SemanticAccepted);
        }
    }
    assert(found_semantic_feedback);
    assert(!backed_up_results.pop());

    // A reservation can always become feedback, even with every other fixed
    // slot occupied. The result no longer has to survive in later snapshots.
    assert(backed_up_results.track(200U));
    for (std::uint32_t id = 1U;
         id < smoker::platform::BlynkCommandResults::capacity; ++id) {
        assert(backed_up_results.record_service_result(id, true));
    }
    assert(!backed_up_results.record_service_result(201U, true));
    assert(!backed_up_results.track(201U));
    assert(backed_up_results.resolve_service_result(200U, false));
    assert(backed_up_results.pending_count() == 0U);
    for (std::uint32_t id = 1U;
         id < smoker::platform::BlynkCommandResults::capacity; ++id) {
        const auto earlier_service = backed_up_results.pop();
        assert(earlier_service && earlier_service->correlation_id == id);
        assert(earlier_service->kind
            == smoker::platform::BlynkCommandResultKind::ServiceAccepted);
    }
    const auto rejected_service = backed_up_results.pop();
    assert(rejected_service && rejected_service->correlation_id == 200U);
    assert(rejected_service->kind
        == smoker::platform::BlynkCommandResultKind::ServiceRejected);
    assert(!backed_up_results.pop());

    assert(backed_up_results.track(201U));
    assert(backed_up_results.cancel(201U));
    assert(!backed_up_results.cancel(201U));
    assert(backed_up_results.pending_count() == 0U);

    for (std::uint32_t id = 1U;
         id <= smoker::platform::BlynkCommandResults::capacity; ++id) {
        assert(backed_up_results.record_service_result(id, true));
    }
    assert(!backed_up_results.record_service_result(999U, false));
    assert(!backed_up_results.track(999U));
    backed_up_results.disconnected();
    assert(backed_up_results.track(999U));
    const std::array after_reconnect{
        smoker::app::CommandResultView{999U, true}
    };
    backed_up_results.observe(after_reconnect);
    const auto after_reconnect_feedback = backed_up_results.pop();
    assert(after_reconnect_feedback
        && after_reconnect_feedback->correlation_id == 999U);

    smoker::platform::BlynkEventScheduler events;
    events.queue(smoker::platform::BlynkEventType::Alarm, "first");
    events.queue(smoker::platform::BlynkEventType::Alarm, "newest");
    auto event = events.pending_publish(0);
    assert(event && std::string_view{event->description.data()} == "newest");
    events.publish_succeeded(event->type, 0);
    events.queue(smoker::platform::BlynkEventType::Alarm, "later");
    assert(!events.pending_publish(4'999).has_value());
    assert(events.pending_publish(5'000).has_value());
    events.disconnected();
    assert(!events.pending_publish(100'000).has_value());
}

smoker::platform::BlynkProvisionedConfiguration provisioning_configuration()
{
    smoker::platform::BlynkProvisionedConfiguration configuration{};
    copy_string(configuration.endpoint, "fra1.blynk.cloud");
    copy_string(configuration.template_id, "TMPL_test_15");
    copy_string(configuration.token, "fixture-secret");
    return configuration;
}

std::vector<std::uint8_t> provisioning_payload(
    const smoker::platform::BlynkProvisionedConfiguration& configuration
)
{
    const std::string_view endpoint{configuration.endpoint.data()};
    const std::string_view template_id{configuration.template_id.data()};
    const std::string_view token{configuration.token.data()};
    std::vector<std::uint8_t> result(6U + endpoint.size() + template_id.size() + token.size());
    const auto put = [&result](const std::size_t offset, const std::size_t value) {
        result[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        result[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    };
    put(0U, endpoint.size()); put(2U, template_id.size()); put(4U, token.size());
    std::size_t cursor = 6U;
    std::memcpy(result.data() + cursor, endpoint.data(), endpoint.size()); cursor += endpoint.size();
    std::memcpy(result.data() + cursor, template_id.data(), template_id.size()); cursor += template_id.size();
    std::memcpy(result.data() + cursor, token.data(), token.size());
    return result;
}

std::vector<std::uint8_t> provisioning_frame(const std::vector<std::uint8_t>& payload)
{
    const auto crc = smoker::platform::blynk_frame_crc32(payload);
    std::array<char, 96U> header{};
    const auto count = std::snprintf(
        header.data(), header.size(), "FUMURI-BLYNK/1 SET %zu %08X\n",
        payload.size(), static_cast<unsigned>(crc)
    );
    assert(count > 0);
    std::vector<std::uint8_t> result(
        reinterpret_cast<const std::uint8_t*>(header.data()),
        reinterpret_cast<const std::uint8_t*>(header.data()) + count
    );
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

void test_provisioning_blob_and_fragmented_parser()
{
    const auto configuration = provisioning_configuration();
    assert(smoker::platform::valid_blynk_configuration(configuration));
    const auto blob = smoker::platform::encode_blynk_configuration(configuration);
    const auto decoded = smoker::platform::decode_blynk_configuration(blob);
    assert(decoded == configuration);
    auto corrupt_blob = blob;
    corrupt_blob[20] ^= 0x01U;
    assert(!smoker::platform::decode_blynk_configuration(corrupt_blob));

    const auto payload = provisioning_payload(configuration);
    const auto frame = provisioning_frame(payload);
    smoker::platform::BlynkProvisioningParser parser;
    std::optional<smoker::platform::BlynkProvisioningRequest> request;
    for (const auto byte : frame) {
        const auto current = parser.consume(byte);
        if (current) request = current;
    }
    assert(request && request->operation == smoker::platform::BlynkProvisioningOperation::Set);
    assert(request->configuration == configuration);
    assert(parser.take_error() == smoker::platform::BlynkProvisioningParseError::None);

    auto invalid_configuration = configuration;
    copy_string(invalid_configuration.endpoint, "not-a-blynk-endpoint.example");
    const auto invalid_frame = provisioning_frame(
        provisioning_payload(invalid_configuration)
    );
    request.reset();
    for (const auto byte : invalid_frame) {
        const auto current = parser.consume(byte);
        if (current) request = current;
    }
    assert(!request.has_value());
    assert(parser.take_error()
        == smoker::platform::BlynkProvisioningParseError::InvalidConfiguration);

    auto corrupt_frame = frame;
    corrupt_frame.back() ^= 0x01U;
    for (const auto byte : corrupt_frame) static_cast<void>(parser.consume(byte));
    assert(parser.take_error() == smoker::platform::BlynkProvisioningParseError::Corrupt);

    const std::string oversized = "FUMURI-BLYNK/1 SET 9999 00000000\n";
    for (const auto byte : oversized) {
        static_cast<void>(parser.consume(static_cast<std::uint8_t>(byte)));
    }
    assert(parser.take_error() == smoker::platform::BlynkProvisioningParseError::Oversized);

    const std::string status = "FUMURI-BLYNK/1 STATUS 0 00000000\n";
    request.reset();
    for (const auto byte : status) {
        const auto current = parser.consume(static_cast<std::uint8_t>(byte));
        if (current) request = current;
    }
    assert(request && request->operation == smoker::platform::BlynkProvisioningOperation::Status);
}

} // namespace

int main()
{
    test_projection_connect_throttle_coalescing_and_retry();
    test_status_timer_normalization_and_serializer_budget();
    test_allowlisted_deterministic_command_mapping();
    test_atomic_start_mapping_and_strict_parser();
    test_atomic_start_release_is_noop_without_feedback();
    test_atomic_start_button_press_release_sequences();
    test_legacy_start_protocol_fails_closed();
    test_atomic_start_feedback_and_correlation();
    test_raw_mailbox_stop_reservation_and_concurrency();
    test_disconnect_reconnect_boundary_discards_old_connection_state();
    test_control_is_independent_of_blynk_transport();
    test_translated_commands_do_not_cross_reconnect_boundary();
    test_shared_ids_wrap_concurrency_and_fair_drain();
    test_results_and_events_are_separate_and_not_replayed();
    test_provisioning_blob_and_fragmented_parser();
    return 0;
}

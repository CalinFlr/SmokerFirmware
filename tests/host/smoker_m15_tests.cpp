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
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using smoker::platform::BlynkRemoteProjection;
using smoker::platform::BlynkRemoteStatus;

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
    copy_string(firmware.current_version, "0.15.0");
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
    const auto target_update = mapper.map("CmdStartTargetC", "125.0", 1U);
    assert(target_update.decision == smoker::platform::BlynkCommandDecision::Accepted);
    assert(!target_update.command.has_value());

    const auto start = mapper.map("CmdStart", "1", 44U);
    const auto* const start_command = std::get_if<smoker::app::StartSessionCommand>(
        &*start.command
    );
    assert(start_command != nullptr && start_command->session_id == 44U);
    assert(start_command->recipe.stage.chamber_target->celsius() == 125.0F);
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

    assert(mapper.map("CmdStartTargetC", "130.0", 1U).decision
        == smoker::platform::BlynkCommandDecision::Accepted);
    mapper.disconnected();
    const auto after_disconnect = mapper.map("CmdStart", "1", 45U);
    const auto* const disconnected_start =
        std::get_if<smoker::app::StartSessionCommand>(&*after_disconnect.command);
    assert(disconnected_start != nullptr);
    assert(disconnected_start->recipe.stage.chamber_target->celsius() == 110.0F);
}

void test_raw_mailbox_stop_reservation_and_concurrency()
{
    smoker::platform::BlynkInboundMailbox mailbox;
    for (std::size_t index = 0U; index < smoker::platform::blynk_inbound_capacity - 1U; ++index) {
        assert(mailbox.push("CmdAcknowledgeAlarm", "1")
            == smoker::platform::BlynkInboundAdmission::Accepted);
    }
    assert(mailbox.push("CmdStart", "1")
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
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    smoker::app::SmokerApplication application{
        chamber, probe_source, heater, clock, events,
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
         id <= smoker::platform::BlynkCommandResults::capacity; ++id) {
        assert(backed_up_results.record_service_result(id, true));
    }
    assert(backed_up_results.track(100U));
    const std::array semantic{smoker::app::CommandResultView{100U, true}};
    backed_up_results.observe(semantic);
    assert(backed_up_results.pending_count() == 1U);
    assert(backed_up_results.pop());
    backed_up_results.observe(semantic);
    assert(backed_up_results.pending_count() == 0U);

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
    test_raw_mailbox_stop_reservation_and_concurrency();
    test_control_is_independent_of_blynk_transport();
    test_shared_ids_wrap_concurrency_and_fair_drain();
    test_results_and_events_are_separate_and_not_replayed();
    test_provisioning_blob_and_fragmented_parser();
    return 0;
}

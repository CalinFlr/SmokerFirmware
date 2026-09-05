#include "smoker/app/command_mailbox.hpp"
#include "smoker/app/snapshot_exchange.hpp"
#include "smoker/platform/local_network_support.hpp"
#include "smoker/platform/runtime_transport_support.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <variant>

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
    if (void* const memory = std::malloc(size)) {
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

using smoker::app::Command;
using smoker::app::MailboxAdmission;
using smoker::app::ProbeSnapshotView;
using smoker::app::SmokerSnapshotView;
using smoker::core::Temperature;

struct TestContext final {
    int failures{0};

    void require(const bool condition, const char* const message)
    {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }
};

Temperature temperature(const float celsius)
{
    const auto value = Temperature::from_celsius(celsius);
    if (!value) {
        std::abort();
    }
    return *value;
}

SmokerSnapshotView snapshot_with_generation(
    const std::uint32_t generation,
    ProbeSnapshotView& probe,
    smoker::core::Alarm& alarm
)
{
    const auto current = temperature(static_cast<float>(generation % 1000U));
    probe.current_temperature = current;
    probe.target_temperature = current;
    alarm.id = generation;
    return SmokerSnapshotView{
        smoker::core::SessionStatus::Running,
        generation,
        smoker::core::Duration{generation},
        smoker::core::StopReason::None,
        current,
        current,
        *smoker::core::HeaterDemand::from_percent(static_cast<float>(generation % 101U)),
        false,
        {},
        std::span<const ProbeSnapshotView>{&probe, 1U},
        std::span<const smoker::core::Alarm>{&alarm, 1U},
        std::nullopt,
        false,
        temperature(150.0F),
        generation,
        {},
    };
}

void test_mailbox_fifo_stop_and_overflow(TestContext& context)
{
    smoker::app::SpscCommandMailbox mailbox;
    context.require(
        mailbox.push(smoker::app::SetProbeEnabledCommand{1U, true}, 41U)
            == MailboxAdmission::Accepted,
        "M12 mailbox admits first FIFO command"
    );
    context.require(
        mailbox.push(smoker::app::SetProbeAlarmEnabledCommand{1U, false})
            == MailboxAdmission::Accepted,
        "M12 mailbox admits second FIFO command"
    );
    context.require(
        mailbox.push(smoker::app::StopSessionCommand{}) == MailboxAdmission::Accepted,
        "M12 mailbox admits Stop"
    );

    Command command{smoker::app::StopSessionCommand{}};
    std::uint32_t correlation_id = 0U;
    context.require(
        mailbox.try_pop(command, &correlation_id) && correlation_id == 41U
            && std::holds_alternative<smoker::app::SetProbeEnabledCommand>(command),
        "M12 mailbox preserves the first command and its correlation id"
    );
    context.require(
        mailbox.try_pop(command)
            && std::holds_alternative<smoker::app::SetProbeAlarmEnabledCommand>(command),
        "M12 mailbox preserves second FIFO command"
    );
    context.require(
        mailbox.try_pop(command)
            && std::holds_alternative<smoker::app::StopSessionCommand>(command),
        "M12 mailbox preserves Stop position"
    );

    smoker::app::SpscCommandMailbox saturated;
    for (std::size_t index = 0U;
         index < smoker::app::SpscCommandMailbox::regular_admission_capacity;
         ++index) {
        context.require(
            saturated.push(smoker::app::AcknowledgeAlarmCommand{
                static_cast<smoker::core::AlarmId>(index + 1U)
            }) == MailboxAdmission::Accepted,
            "M12 mailbox admits every regular-capacity entry"
        );
    }
    context.require(
        saturated.push(smoker::app::AcknowledgeAlarmCommand{99U})
            == MailboxAdmission::Full,
        "M12 mailbox rejects regular overflow"
    );
    context.require(
        saturated.push(smoker::app::StopSessionCommand{}) == MailboxAdmission::Accepted,
        "M12 mailbox reserves the final slot for Stop"
    );
    context.require(
        saturated.push(smoker::app::StopSessionCommand{}) == MailboxAdmission::Full,
        "M12 mailbox reports full after the reserved Stop slot is occupied"
    );
    context.require(
        saturated.overflow_count() == 2U,
        "M12 mailbox exposes rejected transport count"
    );

    smoker::app::SpscCommandMailbox intent;
    context.require(
        intent.push(smoker::app::StopSessionCommand{}) == MailboxAdmission::Accepted,
        "M12 intent sequence admits first Stop"
    );
    context.require(
        intent.push(smoker::app::SetChamberTargetCommand{temperature(90.0F)})
            == MailboxAdmission::Accepted,
        "M12 intent sequence admits intervening command"
    );
    context.require(
        intent.push(smoker::app::StopSessionCommand{}) == MailboxAdmission::Accepted,
        "M12 intent sequence preserves Stop after intervening command"
    );
    context.require(intent.pending() == 3U, "M12 distinct Stop intent remains FIFO");
}

void test_mailbox_concurrency(TestContext& context)
{
    constexpr std::uint32_t command_count = 20000U;
    smoker::app::SpscCommandMailbox mailbox;
    std::atomic_bool ordering_ok{true};

    std::thread producer{[&mailbox]() {
        for (std::uint32_t id = 1U; id <= command_count; ++id) {
            while (mailbox.push(smoker::app::AcknowledgeAlarmCommand{id})
                   == MailboxAdmission::Full) {
                std::this_thread::yield();
            }
        }
    }};
    std::thread consumer{[&mailbox, &ordering_ok]() {
        Command command{smoker::app::StopSessionCommand{}};
        for (std::uint32_t expected = 1U; expected <= command_count;) {
            if (!mailbox.try_pop(command)) {
                std::this_thread::yield();
                continue;
            }
            const auto* const acknowledge =
                std::get_if<smoker::app::AcknowledgeAlarmCommand>(&command);
            if (acknowledge == nullptr || acknowledge->alarm_id != expected) {
                ordering_ok.store(false, std::memory_order_relaxed);
            }
            ++expected;
        }
    }};
    producer.join();
    consumer.join();

    context.require(ordering_ok.load(), "M12 SPSC mailbox remains ordered under concurrency");
    context.require(mailbox.pending() == 0U, "M12 concurrent mailbox drains completely");

    constexpr std::uint32_t stop_pair_count = 10000U;
    smoker::app::SpscCommandMailbox stop_mailbox;
    std::atomic_bool stop_ordering_ok{true};
    std::thread stop_producer{[&stop_mailbox]() {
        for (std::uint32_t id = 1U; id <= stop_pair_count; ++id) {
            while (stop_mailbox.push(smoker::app::AcknowledgeAlarmCommand{id})
                   == MailboxAdmission::Full) {
                std::this_thread::yield();
            }
            while (stop_mailbox.push(smoker::app::StopSessionCommand{})
                   == MailboxAdmission::Full) {
                std::this_thread::yield();
            }
        }
    }};
    std::thread stop_consumer{[&stop_mailbox, &stop_ordering_ok]() {
        Command command{smoker::app::StopSessionCommand{}};
        for (std::uint32_t expected = 1U; expected <= stop_pair_count; ++expected) {
            while (!stop_mailbox.try_pop(command)) {
                std::this_thread::yield();
            }
            const auto* const acknowledge =
                std::get_if<smoker::app::AcknowledgeAlarmCommand>(&command);
            if (acknowledge == nullptr || acknowledge->alarm_id != expected) {
                stop_ordering_ok.store(false, std::memory_order_relaxed);
            }
            while (!stop_mailbox.try_pop(command)) {
                std::this_thread::yield();
            }
            if (!std::holds_alternative<smoker::app::StopSessionCommand>(command)) {
                stop_ordering_ok.store(false, std::memory_order_relaxed);
            }
        }
    }};
    stop_producer.join();
    stop_consumer.join();
    context.require(
        stop_ordering_ok.load(),
        "M12 SPSC mailbox preserves every concurrent Stop after intervening commands"
    );
    context.require(
        stop_mailbox.pending() == 0U,
        "M12 concurrent Stop mailbox drains completely"
    );
}

void test_snapshot_exchange(TestContext& context)
{
    smoker::app::SnapshotExchange exchange{1U, 1U};
    ProbeSnapshotView probe{
        1U,
        "Simulated",
        smoker::core::ProbeRole::Meat,
        std::nullopt,
        std::nullopt,
        true,
        true,
    };
    smoker::core::Alarm alarm{};

    auto source = snapshot_with_generation(1U, probe, alarm);
    const std::array command_results{
        smoker::app::CommandResultView{73U, false},
    };
    source.command_results = command_results;
    context.require(exchange.publish(source), "M12 snapshot publishes initial slot");
    auto first = exchange.acquire();
    context.require(static_cast<bool>(first), "M12 snapshot returns a read lease");
    source = snapshot_with_generation(2U, probe, alarm);
    context.require(exchange.publish(source), "M12 snapshot publishes while prior slot is leased");
    auto second = exchange.acquire();
    source = snapshot_with_generation(3U, probe, alarm);
    context.require(exchange.publish(source), "M12 snapshot uses third preallocated slot");
    auto third = exchange.acquire();
    source = snapshot_with_generation(4U, probe, alarm);
    context.require(
        !exchange.publish(source),
        "M12 snapshot drops instead of waiting when both non-current slots are leased"
    );
    context.require(exchange.dropped_publish_count() == 1U, "M12 snapshot counts dropped publish");

    const auto current = third.view();
    context.require(current.session_id == 3U, "M12 lease remains internally consistent");
    context.require(
        current.probes.size() == 1U && current.probes.front().current_temperature
            && current.chamber_temperature
            && current.probes.front().current_temperature->celsius()
                == current.chamber_temperature->celsius(),
        "M12 copied probe and chamber values belong to one generation"
    );
    context.require(
        current.command_results.empty(),
        "M12 later snapshots replace rather than retain stale command results"
    );

    smoker::app::SnapshotExchange command_exchange{1U, 1U};
    source = snapshot_with_generation(6U, probe, alarm);
    source.command_results = command_results;
    context.require(command_exchange.publish(source), "M12 publishes semantic command results");
    auto command_lease = command_exchange.acquire();
    const auto command_view = command_lease.view();
    context.require(
        command_view.command_results.size() == 1U
            && command_view.command_results.front().correlation_id == 73U
            && !command_view.command_results.front().semantic_accepted,
        "M12 snapshot exchange preserves command id and semantic rejection"
    );

    smoker::app::SnapshotExchange quiet_exchange{1U, 1U};
    source = snapshot_with_generation(5U, probe, alarm);
    allocation_probe::begin();
    const bool published = quiet_exchange.publish(source);
    const auto allocations = allocation_probe::end();
    context.require(published, "M12 allocation test publishes snapshot");
    context.require(allocations == 0U, "M12 critical snapshot publication allocates no C++ heap");
}

void test_control_readiness_latch(TestContext& context)
{
    using Latch = smoker::platform::ControlReadinessLatch;
    static_assert(std::is_same_v<
        decltype(&Latch::observe_cycle),
        bool (Latch::*)(bool, bool) noexcept
    >);

    Latch initial;
    context.require(!initial.ready(), "control readiness starts false");
    context.require(
        !initial.observe_cycle(false, true) && !initial.ready(),
        "failed first snapshot publish leaves control not ready"
    );
    context.require(
        initial.observe_cycle(true, true) && initial.ready(),
        "a later complete published and watchdog-reset cycle transitions ready"
    );

    Latch watchdog_failure;
    context.require(
        !watchdog_failure.observe_cycle(true, false)
            && !watchdog_failure.ready(),
        "failed watchdog reset emits no readiness transition"
    );
    context.require(
        !watchdog_failure.observe_cycle(false, false)
            && !watchdog_failure.ready(),
        "a cycle missing both proofs remains not ready"
    );

    Latch first_complete_cycle;
    context.require(
        first_complete_cycle.observe_cycle(true, true)
            && first_complete_cycle.ready(),
        "the first cycle with both proofs emits the readiness transition"
    );
    for (std::size_t cycle = 0U; cycle < 4U; ++cycle) {
        context.require(
            !first_complete_cycle.observe_cycle(true, true)
                && first_complete_cycle.ready(),
            "readiness remains one-shot across later complete cycles"
        );
    }

    Latch retry_after_publish_failure;
    context.require(
        !retry_after_publish_failure.observe_cycle(false, true),
        "publish failure does not consume the readiness opportunity"
    );
    context.require(
        retry_after_publish_failure.observe_cycle(true, true)
            && retry_after_publish_failure.ready(),
        "readiness retries successfully after a transient publish failure"
    );

    // The compile-time API assertion above is also the fault-independence
    // contract: session, fault, temperature, and heater state cannot be inputs.
}

void test_wifi_fallback_deadline(TestContext& context)
{
    smoker::platform::WifiFallbackCoordinator fallback;
    context.require(
        fallback.arm(false, false),
        "M12 first STA outage arms the SoftAP fallback deadline"
    );
    for (std::size_t attempt = 0U; attempt < 100U; ++attempt) {
        context.require(
            !fallback.arm(false, false),
            "M12 repeated STA disconnects do not postpone the fallback deadline"
        );
    }
    context.require(fallback.armed(), "M12 fallback remains armed during retries");
    context.require(
        fallback.expire(false),
        "M12 expired outage deadline requests SoftAP recovery"
    );
    fallback.cancel();
    context.require(
        !fallback.permit_enable(true),
        "M12 GOT_IP interleaving revokes an expired fallback before AP enable"
    );
    context.require(
        !fallback.arm(true, false),
        "M12 active SoftAP needs no duplicate fallback deadline"
    );
    context.require(
        fallback.arm(false, false),
        "M12 a later independent outage receives a fresh deadline"
    );
    fallback.cancel();
    context.require(
        !fallback.expire(true) && !fallback.armed(),
        "M12 GOT_IP cancellation prevents stale fallback activation"
    );
}

void test_wifi_sta_retry_state(TestContext& context)
{
    smoker::platform::WifiStaRetryState retry;
    context.require(
        retry.action() == smoker::platform::WifiStaRetryAction::Connect,
        "M12 ordinary STA retry reconnects using an applied driver configuration"
    );
    retry.note_configuration_apply_failed();
    context.require(
        retry.action()
            == smoker::platform::WifiStaRetryAction::ReapplyConfiguration,
        "M12 mode/configuration failure retries the complete STA configuration"
    );
    retry.note_configuration_applied();
    context.require(
        retry.action() == smoker::platform::WifiStaRetryAction::Connect,
        "M12 successful STA configuration returns retries to connect-only"
    );
}

void test_snapshot_exchange_concurrency(TestContext& context)
{
    constexpr std::uint32_t generation_count = 10000U;
    smoker::app::SnapshotExchange exchange{1U, 1U};
    std::atomic_bool writer_done{false};
    std::atomic_bool consistent{true};

    std::thread writer{[&exchange, &writer_done]() {
        ProbeSnapshotView probe{
            1U,
            "Simulated",
            smoker::core::ProbeRole::Meat,
            std::nullopt,
            std::nullopt,
            true,
            true,
        };
        smoker::core::Alarm alarm{};
        for (std::uint32_t generation = 1U; generation <= generation_count; ++generation) {
            const auto source = snapshot_with_generation(generation, probe, alarm);
            static_cast<void>(exchange.publish(source));
        }
        writer_done.store(true, std::memory_order_release);
    }};
    std::thread reader{[&exchange, &writer_done, &consistent]() {
        std::size_t observed = 0U;
        while (!writer_done.load(std::memory_order_acquire) || observed < 100U) {
            auto lease = exchange.acquire();
            if (!lease) {
                std::this_thread::yield();
                continue;
            }
            const auto view = lease.view();
            if (!view.session_id || !view.chamber_temperature || view.probes.size() != 1U
                || !view.probes.front().current_temperature || view.active_alarms.size() != 1U
                || view.command_queue_overflow_count != *view.session_id
                || view.active_alarms.front().id != *view.session_id
                || view.chamber_temperature->celsius()
                    != view.probes.front().current_temperature->celsius()) {
                consistent.store(false, std::memory_order_relaxed);
            }
            ++observed;
        }
    }};
    writer.join();
    reader.join();
    context.require(consistent.load(), "M12 snapshot leases never mix published generations");
}

smoker::platform::RawWifiNetwork raw_network(
    const char* const ssid,
    const std::int16_t rssi,
    const std::uint8_t channel,
    const smoker::platform::WifiSecurityCategory security
)
{
    smoker::platform::RawWifiNetwork result;
    result.ssid_length = std::strlen(ssid);
    std::memcpy(result.ssid.data(), ssid, result.ssid_length);
    result.rssi_dbm = rssi;
    result.channel = channel;
    result.security = security;
    return result;
}

void test_wifi_scan_curation(TestContext& context)
{
    std::array<smoker::platform::RawWifiNetwork, 29U> records{};
    records[0] = raw_network(
        "Fumuri", -71, 1U, smoker::platform::WifiSecurityCategory::Wpa2
    );
    records[1] = raw_network(
        "Fumuri", -38, 6U, smoker::platform::WifiSecurityCategory::Wpa3
    );
    records[2] = raw_network(
        "Open", -42, 11U, smoker::platform::WifiSecurityCategory::Open
    );
    records[3].ssid_length = 0U;
    records[4] = raw_network(
        "Bad\x01Name", -40, 3U, smoker::platform::WifiSecurityCategory::Unknown
    );
    records[5].ssid = {0xC3U, 0x28U};
    records[5].ssid_length = 2U;
    records[5].rssi_dbm = -39;
    records[5].channel = 4U;
    for (std::size_t index = 6U; index < records.size(); ++index) {
        // Large enough even for the full decimal representation of size_t;
        // this keeps the fixture valid under GCC's -Wformat-truncation check.
        char ssid[32]{};
        std::snprintf(ssid, sizeof(ssid), "Network-%02zu", index);
        records[index] = raw_network(
            ssid,
            static_cast<std::int16_t>(-45 - static_cast<std::int16_t>(index)),
            static_cast<std::uint8_t>((index % 11U) + 1U),
            smoker::platform::WifiSecurityCategory::Wpa2
        );
    }

    const auto result = smoker::platform::curate_wifi_networks(records);
    context.require(result.count == 20U, "M12 scan limits the result to twenty networks");
    context.require(result.truncated, "M12 scan reports discarded unique networks");
    context.require(
        std::strcmp(result.networks[0].ssid.data(), "Fumuri") == 0
            && result.networks[0].rssi_dbm == -38
            && result.networks[0].channel == 6U
            && result.networks[0].security
                == smoker::platform::WifiSecurityCategory::Wpa3,
        "M12 scan deduplicates by SSID and retains the strongest record"
    );
    context.require(
        result.networks[0].rssi_dbm >= result.networks[1].rssi_dbm,
        "M12 scan sorts strongest signal first"
    );
    bool sanitized_control = false;
    bool sanitized_utf8 = false;
    std::size_t fumuri_count = 0U;
    for (std::size_t index = 0U; index < result.count; ++index) {
        sanitized_control |= std::strcmp(result.networks[index].ssid.data(), "Bad?Name") == 0;
        sanitized_utf8 |= std::strcmp(result.networks[index].ssid.data(), "?(") == 0;
        fumuri_count += std::strcmp(result.networks[index].ssid.data(), "Fumuri") == 0 ? 1U : 0U;
    }
    context.require(sanitized_control, "M12 scan sanitizes control bytes in SSIDs");
    context.require(sanitized_utf8, "M12 scan sanitizes invalid UTF-8 in SSIDs");
    context.require(fumuri_count == 1U, "M12 scan returns each SSID once");
}

void test_http_device_authority(TestContext& context)
{
    using smoker::platform::host_matches_device_authority;
    context.require(
        host_matches_device_authority("192.168.4.1", "192.168.4.1")
            && host_matches_device_authority("192.168.4.1:80", "192.168.4.1"),
        "M12 accepts the exact AP authority with an optional valid port"
    );
    context.require(
        host_matches_device_authority("SMOKER-A1B2C3.LOCAL", "smoker-a1b2c3.local"),
        "M12 treats the mDNS authority as case-insensitive"
    );
    context.require(
        !host_matches_device_authority("attacker.example", "192.168.4.1")
            && !host_matches_device_authority("192.168.4.1.attacker", "192.168.4.1")
            && !host_matches_device_authority("192.168.4.1:", "192.168.4.1")
            && !host_matches_device_authority("192.168.4.1:evil", "192.168.4.1")
            && !host_matches_device_authority("192.168.4.1:65536", "192.168.4.1"),
        "M12 rejects rebinding hosts, prefixes, and invalid ports"
    );
}

void test_http_security_policy(TestContext& context)
{
    using smoker::platform::build_http_command_admission_body;
    using smoker::platform::build_http_error_body;
    using smoker::platform::classify_http_request_scope;
    using smoker::platform::decide_legacy_authentication_migration;
    using smoker::platform::extract_ipv4_mapped_address;
    using smoker::platform::http_content_type_matches;
    using smoker::platform::http_origin_allowed;
    using smoker::platform::HttpRequestScope;
    using smoker::platform::LegacyAuthenticationMigrationAction;
    using smoker::platform::LegacyClaimState;
    using smoker::platform::LegacyPasswordState;
    using smoker::platform::wifi_security_is_supported;
    using smoker::platform::WifiSecurityCategory;

    context.require(
        http_content_type_matches("application/json", "application/json")
            && http_content_type_matches(
                " Application/JSON ; charset=utf-8", "application/json"
            )
            && !http_content_type_matches("application/jsonp", "application/json")
            && !http_content_type_matches("text/plain", "application/json")
            && !http_content_type_matches("application/json; ", "application/json"),
        "M12 accepts only the intended HTTP media type"
    );
    context.require(
        http_origin_allowed(
            "smoker.local", std::string_view{"http://smoker.local"}, true
        )
            && !http_origin_allowed("smoker.local", std::nullopt, true)
            && http_origin_allowed("smoker.local", std::nullopt, false)
            && !http_origin_allowed(
                "smoker.local", std::string_view{"http://attacker.local"}, true
            ),
        "M12 requires exact Origin for every state-changing request"
    );
    context.require(
        !wifi_security_is_supported(WifiSecurityCategory::Open)
            && wifi_security_is_supported(WifiSecurityCategory::Wpa2)
            && wifi_security_is_supported(WifiSecurityCategory::Wpa3)
            && !wifi_security_is_supported(WifiSecurityCategory::Wep)
            && !wifi_security_is_supported(WifiSecurityCategory::Wpa)
            && !wifi_security_is_supported(WifiSecurityCategory::Enterprise)
            && !wifi_security_is_supported(WifiSecurityCategory::Unknown),
        "M12 exposes only WPA2 and WPA3 Personal as supported"
    );
    context.require(
        classify_http_request_scope(
            smoker::platform::commissioning_ipv4, 0xC0A8649DU, true
        ) == HttpRequestScope::Commissioning
            && classify_http_request_scope(0xC0A8649DU, 0xC0A8649DU, false)
                == HttpRequestScope::Operational
            && classify_http_request_scope(0xC0A8649DU, 0xC0A8649DU, true)
                == HttpRequestScope::Rejected
            && classify_http_request_scope(0x7F000001U, 0xC0A8649DU, false)
                == HttpRequestScope::Rejected
            && classify_http_request_scope(0xC0A8649DU, std::nullopt, false)
                == HttpRequestScope::Rejected
            && classify_http_request_scope(
                   smoker::platform::commissioning_ipv4,
                   std::nullopt,
                   false
               )
                == HttpRequestScope::Rejected,
        "M12 rejects operational scope during open-AP overlap and classifies other addresses fail-closed"
    );
    constexpr std::array<std::uint8_t, 16U> mapped_ap{
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0xFFU, 0xFFU,
        192U, 168U, 4U, 1U,
    };
    constexpr std::array<std::uint8_t, 16U> native_ipv6{
        0xFEU, 0x80U, 0U, 0U, 0U, 0U, 0U, 0U,
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U,
    };
    context.require(
        extract_ipv4_mapped_address(mapped_ap)
                == smoker::platform::commissioning_ipv4
            && !extract_ipv4_mapped_address(native_ipv6),
        "M12 normalizes only IPv4-mapped dual-stack socket addresses"
    );

    allocation_probe::begin();
    const auto error_body = build_http_error_body("memorie JSON indisponibilă");
    const auto error_allocations = allocation_probe::end();
    context.require(
        error_allocations == 0U
            && std::string_view{error_body.bytes.data(), error_body.length}
                == "{\"error\":\"memorie JSON indisponibilă\"}",
        "M12 builds the low-memory HTTP error response without allocating"
    );
    constexpr std::array<char, 256U> oversized_message{};
    const auto bounded_error = build_http_error_body(std::string_view{
        oversized_message.data(), oversized_message.size()
    });
    context.require(
        bounded_error.length < bounded_error.bytes.size()
            && std::string_view{bounded_error.bytes.data(), bounded_error.length}
                == "{\"error\":\"eroare internă\"}",
        "M12 replaces an unbounded HTTP error message with valid bounded JSON"
    );

    allocation_probe::begin();
    const auto admission_body = build_http_command_admission_body(4294967295U);
    const auto admission_allocations = allocation_probe::end();
    context.require(
        admission_allocations == 0U
            && std::string_view{
                   admission_body.bytes.data(), admission_body.length
               }
                == "{\"status\":\"accepted\",\"coalesced_stop\":false,\"command_id\":4294967295}",
        "M12 builds the command admission response before enqueue without allocating"
    );

    context.require(
        decide_legacy_authentication_migration(
            LegacyPasswordState::Missing, false, LegacyClaimState::Missing
        ) == LegacyAuthenticationMigrationAction::UseInitial
            && decide_legacy_authentication_migration(
                   LegacyPasswordState::Valid,
                   false,
                   LegacyClaimState::Claimed
               ) == LegacyAuthenticationMigrationAction::PreserveClaimed
            && decide_legacy_authentication_migration(
                   LegacyPasswordState::Valid,
                   true,
                   LegacyClaimState::Missing
               ) == LegacyAuthenticationMigrationAction::PreserveInitial,
        "M12 migrates only fresh/unclaimed or valid legacy authentication state"
    );
    context.require(
        decide_legacy_authentication_migration(
            LegacyPasswordState::Invalid, false, LegacyClaimState::Claimed
        ) == LegacyAuthenticationMigrationAction::Reject
            && decide_legacy_authentication_migration(
                   LegacyPasswordState::Valid,
                   false,
                   LegacyClaimState::Invalid
               ) == LegacyAuthenticationMigrationAction::Reject
            && decide_legacy_authentication_migration(
                   LegacyPasswordState::Missing,
                   false,
                   LegacyClaimState::Claimed
               ) == LegacyAuthenticationMigrationAction::Reject,
        "M12 rejects unreadable, corrupt, and claimed-without-password legacy state"
    );
}

void test_http_session_lifecycle(TestContext& context)
{
    smoker::platform::HttpSessionState session;
    constexpr std::int64_t second = 1000LL * 1000LL;
    const std::string_view first =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    const std::string_view second_token =
        "f0e0d0c0b0a090807060504030201000ffeeddccbbaa99887766554433221100";
    context.require(
        session.replace(first, 0) && session.validate(first, 29LL * 60LL * second),
        "M12 session accepts one 256-bit token and refreshes its idle deadline"
    );
    context.require(
        session.replace(second_token, 30LL * 60LL * second)
            && !session.validate(first, 30LL * 60LL * second)
            && session.validate(second_token, 30LL * 60LL * second),
        "M12 a new login invalidates the previous session token"
    );
    context.require(
        !session.validate(second_token, 61LL * 60LL * second) && !session.active(),
        "M12 session expires after thirty idle minutes"
    );
    context.require(
        session.replace(first, 62LL * 60LL * second),
        "M12 session can be re-established after expiry"
    );
    session.invalidate();
    context.require(
        !session.validate(first, 62LL * 60LL * second) && !session.active(),
        "M12 logout or password change invalidates the active session"
    );
}

void test_login_rate_limiter(TestContext& context)
{
    smoker::platform::LoginRateLimiter limiter;
    constexpr std::uint32_t peer = 0x01020304U;
    constexpr std::int64_t second = 1000LL * 1000LL;
    for (std::uint32_t failure = 0U; failure < 4U; ++failure) {
        limiter.record_failure(peer, static_cast<std::int64_t>(failure) * second);
        context.require(
            limiter.permit(peer, static_cast<std::int64_t>(failure) * second),
            "M12 permits the first four failed login attempts"
        );
    }
    limiter.record_failure(peer, 4LL * second);
    context.require(
        !limiter.permit(peer, 4LL * second)
            && limiter.retry_after_seconds(peer, 4LL * second) == 30U
            && limiter.permit(0x05060708U, 4LL * second),
        "M12 rate limits the fifth failure without blocking another peer"
    );
    context.require(
        limiter.permit(peer, 34LL * second),
        "M12 releases a peer after the retry interval"
    );
    limiter.record_success(peer);
    context.require(
        limiter.permit(peer, 34LL * second)
            && limiter.retry_after_seconds(peer, 34LL * second) == 0U,
        "M12 successful login clears peer rate-limit state"
    );
}

void test_wifi_scan_transitions(TestContext& context)
{
    smoker::platform::WifiScanCoordinator coordinator;
    const auto first = coordinator.request(true, false);
    context.require(first.start_scan, "M12 starts the first requested scan");
    context.require(
        coordinator.blocks_sta_reconnect(),
        "M12 serializes STA reconnect while a scan is active"
    );
    const auto combined = coordinator.request(true, false);
    context.require(!combined.start_scan, "M12 combines repeated requests into the active scan");
    coordinator.note_sta_disconnected(true);
    const auto complete = coordinator.complete(false);
    context.require(
        complete.reconnect_sta && !coordinator.blocks_sta_reconnect()
            && coordinator.state() == smoker::platform::WifiScanState::Complete,
        "M12 resumes configured STA after scan completion"
    );

    static_cast<void>(coordinator.request(false, false));
    const auto failure = coordinator.fail(false);
    context.require(
        !failure.reconnect_sta
            && coordinator.state() == smoker::platform::WifiScanState::Error,
        "M12 exposes a failed scan without inventing a STA reconnect"
    );
}

void test_captive_dns_shutdown_wait(TestContext& context)
{
    using smoker::platform::wait_for_captive_dns_shutdown;
    constexpr std::int64_t tick_microseconds = 10'000; // Production: 100 Hz.

    // Model the priority relationship: sys_evt can only let the lower-priority
    // DNS worker finish when it blocks, not when it calls vTaskDelay(0).
    for (const auto phase : {0LL, 1LL, 5'000LL, 9'999LL}) {
        std::int64_t now = phase;
        bool task_present = true;
        bool exited = false;
        std::uint32_t blocking_delays = 0U;
        wait_for_captive_dns_shutdown(
            [&]() noexcept { return now; },
            [&]() noexcept { return task_present && !exited; },
            [&](const std::uint32_t ticks) noexcept {
                context.require(ticks == 1U, "DNS shutdown blocks for one actual tick");
                if (ticks == 0U) std::abort(); // A zero-tick wait cannot make progress.
                now = (now / tick_microseconds + ticks) * tick_microseconds;
                ++blocking_delays;
                exited = true;
            }
        );
        context.require(
            exited && blocking_delays == 1U,
            "DNS worker exits during the stop handler before a consecutive AP start"
        );
        context.require(
            task_present,
            "DNS wait leaves task deletion and static-storage ownership to its caller"
        );
        const auto completed_at = now;
        wait_for_captive_dns_shutdown(
            [&]() noexcept { return now; },
            [&]() noexcept { return task_present && !exited; },
            [&](const std::uint32_t) noexcept { ++blocking_delays; }
        );
        task_present = false;
        exited = false;
        wait_for_captive_dns_shutdown(
            [&]() noexcept { return now; },
            [&]() noexcept { return task_present && !exited; },
            [&](const std::uint32_t) noexcept { ++blocking_delays; }
        );
        context.require(
            now == completed_at && blocking_delays == 1U,
            "DNS shutdown does not wait for an exited or absent task"
        );
    }

    // A worker stuck in receive can take its configured 250 ms timeout to
    // observe shutdown. It must get that time, while a stuck worker must not
    // turn the old 400-iteration count into four seconds of event-loop delay.
    for (const bool worker_finishes : {true, false}) {
        std::int64_t now = 9'999;
        const auto started_at = now;
        const auto worker_exit_at = now + 250'000;
        bool exited = false;
        std::uint32_t blocking_delays = 0U;
        wait_for_captive_dns_shutdown(
            [&]() noexcept { return now; },
            [&]() noexcept { return !exited; },
            [&](const std::uint32_t ticks) noexcept {
                context.require(ticks == 1U, "DNS deadline wait keeps each delay to one tick");
                if (ticks == 0U) std::abort();
                now = (now / tick_microseconds + ticks) * tick_microseconds;
                ++blocking_delays;
                exited = worker_finishes && now >= worker_exit_at;
            }
        );
        const auto elapsed = now - started_at;
        if (worker_finishes) {
            context.require(
                exited && elapsed >= 250'000 && elapsed < 260'000,
                "DNS shutdown gives receive-timeout cleanup an actual scheduling opportunity"
            );
        } else {
            context.require(
                !exited && elapsed >= 400'000 && elapsed < 410'000
                    && blocking_delays <= 41U,
                "DNS shutdown stops retrying at 400 ms without claiming worker exit"
            );
        }
    }
}

std::array<std::uint8_t, 31U> dns_query(const std::uint16_t type)
{
    std::array<std::uint8_t, 31U> query{
        0x12U, 0x34U, 0x01U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U,
        0x07U, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
        0x05U, 'l', 'o', 'c', 'a', 'l', 0x00U,
        0x00U, 0x00U, 0x00U, 0x01U,
    };
    query[27] = static_cast<std::uint8_t>((type >> 8U) & 0xFFU);
    query[28] = static_cast<std::uint8_t>(type & 0xFFU);
    return query;
}

void test_captive_dns_parser(TestContext& context)
{
    const auto query = dns_query(1U);
    std::array<std::uint8_t, 128U> response{};
    const auto answer = smoker::platform::build_captive_dns_response(
        query, response, {192U, 168U, 4U, 1U}
    );
    context.require(
        answer.status == smoker::platform::DnsResponseStatus::Answer
            && answer.length == query.size() + 16U,
        "M12 DNS parser answers a valid A query"
    );
    context.require(
        response[6] == 0U && response[7] == 1U
            && response[answer.length - 4U] == 192U
            && response[answer.length - 1U] == 1U,
        "M12 DNS answer redirects to the SoftAP address"
    );

    const auto aaaa = dns_query(28U);
    const auto no_answer = smoker::platform::build_captive_dns_response(
        aaaa, response, {192U, 168U, 4U, 1U}
    );
    context.require(
        no_answer.status == smoker::platform::DnsResponseStatus::NoAnswer
            && no_answer.length == aaaa.size(),
        "M12 DNS parser returns a valid empty response for unsupported query types"
    );

    auto invalid = query;
    invalid[12] = 0xC0U;
    context.require(
        smoker::platform::build_captive_dns_response(
            invalid, response, {192U, 168U, 4U, 1U}
        ).status == smoker::platform::DnsResponseStatus::Invalid,
        "M12 DNS parser rejects compressed question names"
    );
    context.require(
        smoker::platform::build_captive_dns_response(
            std::span<const std::uint8_t>{query.data(), query.size() - 2U},
            response,
            {192U, 168U, 4U, 1U}
        ).status == smoker::platform::DnsResponseStatus::Truncated,
        "M12 DNS parser rejects truncated questions without reading past the packet"
    );
}

} // namespace

int main()
{
    TestContext context;
    test_mailbox_fifo_stop_and_overflow(context);
    test_mailbox_concurrency(context);
    test_snapshot_exchange(context);
    test_control_readiness_latch(context);
    test_snapshot_exchange_concurrency(context);
    test_wifi_scan_curation(context);
    test_http_device_authority(context);
    test_http_security_policy(context);
    test_http_session_lifecycle(context);
    test_login_rate_limiter(context);
    test_wifi_scan_transitions(context);
    test_wifi_fallback_deadline(context);
    test_wifi_sta_retry_state(context);
    test_captive_dns_shutdown_wait(context);
    test_captive_dns_parser(context);
    if (context.failures == 0) {
        std::puts("M12 transport tests: PASS");
        return 0;
    }
    std::fprintf(stderr, "M12 transport tests: %d failure(s)\n", context.failures);
    return 1;
}

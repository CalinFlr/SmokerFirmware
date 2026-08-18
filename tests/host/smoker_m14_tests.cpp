#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/history_support.hpp"
#include "smoker/platform/flash_operation_coordinator.hpp"
#include "smoker/platform/local_network_support.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace allocation_probe {
bool enabled = false;
std::size_t allocations = 0U;
void begin() noexcept { allocations = 0U; enabled = true; }
std::size_t end() noexcept { enabled = false; return allocations; }
} // namespace allocation_probe

void* operator new(const std::size_t size)
{
    if (allocation_probe::enabled) ++allocation_probe::allocations;
    if (auto* memory = std::malloc(size)) return memory;
    std::abort();
}
void* operator new[](const std::size_t size) { return ::operator new(size); }
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { ::operator delete(memory); }
void operator delete(void* const memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { ::operator delete(memory); }

namespace {

using smoker::app::ProbeSnapshotView;
using smoker::app::SmokerSnapshotView;
using smoker::core::Duration;
using smoker::core::SessionStatus;
using smoker::platform::CircularHistoryLog;
using smoker::platform::HistoryObservation;
using smoker::platform::HistoryObservationKind;
using smoker::platform::HistoryObservationMailbox;
using smoker::platform::HistoryStorageState;
using smoker::platform::IHistoryFlash;

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

smoker::core::Temperature temperature(const float celsius)
{
    const auto result = smoker::core::Temperature::from_celsius(celsius);
    if (!result) std::abort();
    return *result;
}

class MemoryFlash final : public IHistoryFlash {
public:
    explicit MemoryFlash(const std::size_t sectors, const bool random = false)
        : bytes_(sectors * smoker::platform::history_page_bytes, 0xFFU)
    {
        if (random) {
            std::uint32_t state = 0x12345678U;
            for (auto& byte : bytes_) {
                state = state * 1664525U + 1013904223U;
                byte = static_cast<std::uint8_t>(state >> 24U);
            }
        }
    }

    std::size_t size() const noexcept override { return bytes_.size(); }
    std::size_t sector_size() const noexcept override
    {
        return smoker::platform::history_page_bytes;
    }
    bool read(
        const std::size_t offset, const std::span<std::uint8_t> destination
    ) const noexcept override
    {
        ++read_calls_;
        if (read_calls_ == fail_read_call_) return false;
        if (offset + destination.size() > bytes_.size()) return false;
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                    destination.size(), destination.begin());
        return true;
    }
    bool write(
        const std::size_t offset, const std::span<const std::uint8_t> source
    ) noexcept override
    {
        ++write_calls_;
        if (fail_all_writes_ || write_calls_ == fail_write_call_) return false;
        if (offset + source.size() > bytes_.size()) return false;
        for (std::size_t index = 0U; index < source.size(); ++index) {
            const auto old_value = bytes_[offset + index];
            if ((old_value & source[index]) != source[index]) return false;
        }
        for (std::size_t index = 0U; index < source.size(); ++index) {
            bytes_[offset + index] &= source[index];
        }
        return true;
    }
    bool erase_sector(const std::size_t offset) noexcept override
    {
        ++erase_calls_;
        if (erase_calls_ == fail_erase_call_) {
            if (partially_erase_on_failure_ && offset + 40U <= bytes_.size()) {
                for (std::size_t index = offset + 36U; index < offset + 40U; ++index) {
                    for (std::uint8_t bit = 1U; bit != 0U; bit <<= 1U) {
                        if ((bytes_[index] & bit) == 0U) {
                            bytes_[index] |= bit;
                            return false;
                        }
                    }
                }
            }
            return false;
        }
        if (offset % sector_size() != 0U || offset + sector_size() > bytes_.size()) {
            return false;
        }
        std::fill_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                    sector_size(), 0xFFU);
        return true;
    }
    void fail_next_write_after(const std::size_t successful_writes) noexcept
    {
        fail_write_call_ = write_calls_ + successful_writes + 1U;
    }
    void stop_failing() noexcept { fail_write_call_ = std::numeric_limits<std::size_t>::max(); }
    void fail_all_writes() noexcept { fail_all_writes_ = true; }
    void fail_next_read_after(const std::size_t successful_reads) noexcept
    {
        fail_read_call_ = read_calls_ + successful_reads + 1U;
    }
    void stop_failing_reads() noexcept
    {
        fail_read_call_ = std::numeric_limits<std::size_t>::max();
    }
    void fail_next_erase_after(
        const std::size_t successful_erases, const bool partially_erase = false
    ) noexcept
    {
        fail_erase_call_ = erase_calls_ + successful_erases + 1U;
        partially_erase_on_failure_ = partially_erase;
    }
    [[nodiscard]] std::size_t erase_calls() const noexcept { return erase_calls_; }
    void corrupt(const std::size_t offset) noexcept { bytes_[offset] ^= 0x01U; }

private:
    std::vector<std::uint8_t> bytes_;
    mutable std::size_t read_calls_{0U};
    std::size_t fail_read_call_{std::numeric_limits<std::size_t>::max()};
    std::size_t write_calls_{0U};
    std::size_t fail_write_call_{std::numeric_limits<std::size_t>::max()};
    std::size_t erase_calls_{0U};
    std::size_t fail_erase_call_{std::numeric_limits<std::size_t>::max()};
    bool partially_erase_on_failure_{false};
    bool fail_all_writes_{false};
};

HistoryObservation observation(
    const HistoryObservationKind kind,
    const std::int64_t elapsed_ms,
    const SessionStatus status = SessionStatus::Running
)
{
    HistoryObservation value;
    value.kind = kind;
    value.application_session_id = 77U;
    value.session_status = status;
    value.session_elapsed = Duration{elapsed_ms};
    value.chamber_temperature = temperature(80.0F);
    value.chamber_target = temperature(110.0F);
    value.heater_demand = *smoker::core::HeaterDemand::from_percent(40.0F);
    value.probes.push_back({
        1U, smoker::core::ProbeRole::Meat, temperature(60.0F), temperature(75.0F),
        true, true
    });
    return value;
}

void test_empty_random_and_reboot(TestContext& context)
{
    MemoryFlash empty{4U};
    CircularHistoryLog log{empty};
    context.require(log.initialize(), "empty flash initializes");
    context.require(log.sessions(std::nullopt, 32U).empty(), "empty flash has no sessions");
    context.require(log.health().state == HistoryStorageState::Ready,
                    "empty flash is ready");

    MemoryFlash random{4U, true};
    CircularHistoryLog random_log{random};
    context.require(random_log.initialize(), "random unclaimed media initializes lazily");
    context.require(random_log.sessions(std::nullopt, 32U).empty(),
                    "random unclaimed media exposes no invented sessions");

    auto start = observation(HistoryObservationKind::Start, 0);
    start.unix_utc_seconds.reset();
    const auto first_id = log.begin_session(start);
    context.require(first_id == 1U, "first durable history id is one");
    auto sample = observation(HistoryObservationKind::Sample, 60'000);
    context.require(log.append(sample), "periodic sample appends");
    auto change = observation(HistoryObservationKind::Change, 61'000);
    change.unix_utc_seconds = 1'787'000'000;
    context.require(log.append(change), "UTC may appear mid-session");
    auto end = observation(HistoryObservationKind::End, 62'000, SessionStatus::Stopped);
    end.stop_reason = smoker::core::StopReason::User;
    end.unix_utc_seconds = 1'787'000'001;
    context.require(log.append(end), "end record appends");

    CircularHistoryLog rebooted{empty};
    context.require(rebooted.initialize(), "valid log reconstructs after reboot");
    const auto summaries = rebooted.sessions(std::nullopt, 32U);
    context.require(summaries.size() == 1U && summaries.front().history_id == 1U,
                    "reboot reconstructs durable session");
    context.require(summaries.front().sample_count == 1U
                        && !summaries.front().interrupted,
                    "summary reconstructs sample and completed lifecycle");
    context.require(!summaries.front().start_unix_utc_seconds
                        && summaries.front().end_unix_utc_seconds.has_value(),
                    "optional UTC can first appear after start");
    context.require(rebooted.begin_session(observation(HistoryObservationKind::Start, 0)) == 2U,
                    "history id allocation survives reboot");

    empty.fail_next_read_after(0U);
    CircularHistoryLog unreadable{empty};
    context.require(!unreadable.initialize(),
                    "unreadable history media does not initialize as reusable space");
    empty.stop_failing_reads();
    context.require(unreadable.initialize(),
                    "history initialization can retry after a transient read failure");
    const auto recovered = unreadable.sessions(std::nullopt, 32U);
    context.require(recovered.size() == 2U && recovered.back().history_id == 1U,
                    "read retry preserves previously committed history");
}

void test_torn_and_corrupt_records(TestContext& context)
{
    MemoryFlash torn_page_flash{2U};
    CircularHistoryLog torn_page_log{torn_page_flash};
    context.require(torn_page_log.initialize(), "torn page test initializes");
    torn_page_flash.fail_next_write_after(1U);
    context.require(!torn_page_log.begin_session(
                        observation(HistoryObservationKind::Start, 0)
                    ).has_value(), "missing page commit rejects session start");
    torn_page_flash.stop_failing();
    CircularHistoryLog torn_page_reboot{torn_page_flash};
    context.require(torn_page_reboot.initialize()
                        && torn_page_reboot.health().state == HistoryStorageState::Degraded,
                    "page commit-last recovery skips the torn page");

    MemoryFlash torn_flash{3U};
    CircularHistoryLog log{torn_flash};
    context.require(log.initialize(), "torn test initializes");
    context.require(log.begin_session(observation(HistoryObservationKind::Start, 0)).has_value(),
                    "torn test start appends");
    torn_flash.fail_next_write_after(1U); // record header succeeds, payload does not
    context.require(!log.append(observation(HistoryObservationKind::Sample, 60'000)),
                    "torn payload reports append failure");
    torn_flash.stop_failing();
    context.require(log.append(observation(HistoryObservationKind::Change, 61'000)),
                    "runtime continues on a fresh page after a torn program");
    CircularHistoryLog rebooted{torn_flash};
    context.require(rebooted.initialize(), "torn record does not prevent reconstruction");
    context.require(rebooted.health().state == HistoryStorageState::Degraded,
                    "torn record is reported degraded");
    context.require(rebooted.sessions(std::nullopt, 8U).size() == 1U,
                    "committed start survives later torn record");

    MemoryFlash corrupt_flash{3U};
    CircularHistoryLog clean{corrupt_flash};
    context.require(clean.initialize(), "corruption test initializes");
    context.require(clean.begin_session(observation(HistoryObservationKind::Start, 0)).has_value(),
                    "corruption test start appends");
    corrupt_flash.corrupt(40U + 40U + 12U);
    CircularHistoryLog corrupted{corrupt_flash};
    context.require(corrupted.initialize(), "CRC corruption remains recoverable");
    context.require(corrupted.health().corrupt_records > 0U,
                    "CRC corruption increments counter");

    MemoryFlash live_corruption_flash{2U};
    CircularHistoryLog live_corruption_log{live_corruption_flash};
    context.require(live_corruption_log.initialize(), "live corruption test initializes");
    const auto live_history_id = live_corruption_log.begin_session(
        observation(HistoryObservationKind::Start, 0)
    );
    context.require(live_history_id.has_value(), "live corruption session starts");
    std::optional<std::uint32_t> continuation;
    context.require(
        live_corruption_log.samples(*live_history_id, std::nullopt, 8U, 1U, continuation)
            .size() == 1U,
        "committed live record is queryable before corruption"
    );
    // Offset 84 is the first record payload's application_session_id. It remains
    // decodable after a bit flip, so this proves the query path verifies payload CRC.
    live_corruption_flash.corrupt(40U + 40U + 4U);
    const auto rejected_live_records = live_corruption_log.samples(
        *live_history_id, std::nullopt, 8U, 1U, continuation
    );
    context.require(rejected_live_records.empty()
                        && live_corruption_log.last_query_failed(),
                    "query-time payload corruption is rejected instead of returned");
    context.require(live_corruption_log.health().corrupt_records > 0U
                        && live_corruption_log.health().state == HistoryStorageState::Degraded,
                    "query-time corruption is reflected in history health");

    MemoryFlash torn_commit_flash{2U};
    CircularHistoryLog commit_log{torn_commit_flash};
    context.require(commit_log.initialize(), "torn commit test initializes");
    context.require(commit_log.begin_session(observation(HistoryObservationKind::Start, 0)).has_value(),
                    "torn commit test starts");
    torn_commit_flash.fail_next_write_after(2U);
    context.require(!commit_log.append(observation(HistoryObservationKind::Sample, 60'000)),
                    "missing commit marker rejects append");
    torn_commit_flash.stop_failing();
    CircularHistoryLog commit_reboot{torn_commit_flash};
    context.require(commit_reboot.initialize()
                        && commit_reboot.health().state == HistoryStorageState::Degraded,
                    "commit-last recovery ignores an uncommitted payload");

    MemoryFlash failed_flash{2U};
    CircularHistoryLog failed_log{failed_flash};
    context.require(failed_log.initialize(), "failure-state test initializes");
    failed_flash.fail_all_writes();
    for (int attempt = 0; attempt < 3; ++attempt) {
        context.require(!failed_log.begin_session(
                            observation(HistoryObservationKind::Start, 0)
                        ).has_value(), "repeated storage failure remains auxiliary");
    }
    context.require(failed_log.health().state == HistoryStorageState::Failed,
                    "three consecutive storage failures report FAILED");

    MemoryFlash start_retry_flash{3U};
    CircularHistoryLog start_retry_log{start_retry_flash};
    context.require(start_retry_log.initialize(), "START retry test initializes");
    start_retry_flash.fail_next_write_after(0U);
    context.require(!start_retry_log.begin_session(
                        observation(HistoryObservationKind::Start, 0)
                    ).has_value(), "transient START write failure is reported");
    start_retry_flash.stop_failing();
    const auto retried_start = start_retry_log.begin_session(
        observation(HistoryObservationKind::Start, 0)
    );
    context.require(retried_start.has_value(), "the same lifecycle START can be retried");
    auto retried_start_end = observation(
        HistoryObservationKind::End, 1'000, SessionStatus::Stopped
    );
    retried_start_end.stop_reason = smoker::core::StopReason::User;
    context.require(start_retry_log.append(retried_start_end),
                    "END closes a session after its START retry succeeds");

    MemoryFlash repeated_record_flash{4U};
    CircularHistoryLog repeated_record_log{repeated_record_flash};
    context.require(repeated_record_log.initialize(), "record failure-state test initializes");
    context.require(repeated_record_log.begin_session(
                        observation(HistoryObservationKind::Start, 0)
                    ).has_value(), "record failure-state session starts");
    for (int attempt = 0; attempt < 3; ++attempt) {
        // The first failed record reuses the START page; later retries must
        // allocate and commit a fresh page before reaching the payload write.
        repeated_record_flash.fail_next_write_after(attempt == 0 ? 1U : 3U);
        context.require(!repeated_record_log.append(observation(
                            HistoryObservationKind::Sample,
                            60'000 + static_cast<std::int64_t>(attempt) * 60'000
                        )), "failed record after fresh page allocation is counted");
    }
    context.require(repeated_record_log.health().state == HistoryStorageState::Failed,
                    "three failed records after page allocation report FAILED");

    MemoryFlash end_flash{3U};
    CircularHistoryLog end_log{end_flash};
    context.require(end_log.initialize(), "END retry test initializes");
    const auto end_history_id = end_log.begin_session(observation(HistoryObservationKind::Start, 0));
    context.require(end_history_id.has_value(), "END retry session starts");
    auto end = observation(HistoryObservationKind::End, 60'000, SessionStatus::Stopped);
    end.stop_reason = smoker::core::StopReason::User;
    end_flash.fail_next_write_after(1U);
    context.require(!end_log.append(end), "torn END reports append failure");
    end_flash.stop_failing();
    context.require(end_log.append(end), "the same END can be durably retried");
    const auto ended_sessions = end_log.sessions(std::nullopt, 8U);
    context.require(ended_sessions.size() == 1U && !ended_sessions.front().active
                        && !ended_sessions.front().interrupted,
                    "retried END closes the durable session");
}

void test_pagination_and_stride(TestContext& context)
{
    MemoryFlash flash{16U};
    CircularHistoryLog log{flash};
    context.require(log.initialize(), "pagination flash initializes");
    const auto history_id = log.begin_session(
        observation(HistoryObservationKind::Start, 0)
    );
    context.require(history_id.has_value(), "pagination session starts");
    for (std::int64_t index = 1; index <= 12; ++index) {
        context.require(log.append(observation(
                            HistoryObservationKind::Sample, index * 60'000
                        )), "pagination sample appends");
        if (index == 5) {
            context.require(log.append(observation(
                                HistoryObservationKind::Change, index * 60'000 + 1
                            )), "pagination change appends");
        }
    }
    context.require(log.append(observation(
                        HistoryObservationKind::End, 780'000, SessionStatus::Stopped
                    )), "pagination end appends");

    std::optional<std::uint32_t> continuation;
    const auto first = log.samples(*history_id, std::nullopt, 4U, 3U, continuation);
    context.require(first.size() == 4U && first.front().kind == HistoryObservationKind::Start
                        && continuation.has_value(),
                    "bounded sample page exposes a continuation cursor");
    context.require(std::any_of(first.begin(), first.end(), [](const auto& value) {
        return value.kind == HistoryObservationKind::Change;
    }), "stride never suppresses lifecycle or change observations");

    const auto second = log.samples(*history_id, continuation, 60U, 3U, continuation);
    context.require(!second.empty()
                        && second.back().kind == HistoryObservationKind::End
                        && !continuation.has_value(),
                    "cursor resumes strictly after the previous page and reaches END");
}

void test_rollover_eviction_truncation_and_interruption(TestContext& context)
{
    MemoryFlash flash{3U};
    CircularHistoryLog log{flash};
    context.require(log.initialize(), "rollover flash initializes");
    const auto first = log.begin_session(observation(HistoryObservationKind::Start, 0));
    context.require(first.has_value(), "first rollover session starts");
    for (std::int64_t index = 1; index <= 35; ++index) {
        context.require(log.append(observation(HistoryObservationKind::Sample, index * 60'000)),
                        "first session rolls sectors");
    }
    auto end = observation(HistoryObservationKind::End, 2'200'000, SessionStatus::Stopped);
    end.stop_reason = smoker::core::StopReason::User;
    context.require(log.append(end), "first rollover session completes");
    const auto second = log.begin_session(observation(HistoryObservationKind::Start, 0));
    context.require(second.has_value() && *second > *first, "second session starts after rollover");
    for (std::int64_t index = 1; index <= 130; ++index) {
        context.require(log.append(observation(HistoryObservationKind::Sample, index * 60'000)),
                        "second session appends while evicting completed history");
    }
    const auto summaries = log.sessions(std::nullopt, 8U);
    context.require(!summaries.empty() && summaries.front().history_id == *second,
                    "newest session remains after eviction");
    context.require(std::none_of(summaries.begin(), summaries.end(), [first](const auto& summary) {
        return summary.history_id == *first;
    }), "oldest completed session is evicted first");
    context.require(summaries.front().truncated,
                    "single long session reports newest-page truncation");

    CircularHistoryLog rebooted{flash};
    context.require(rebooted.initialize(), "long session reconstructs after reboot");
    const auto interrupted = rebooted.sessions(std::nullopt, 8U);
    context.require(!interrupted.empty() && interrupted.front().interrupted,
                    "session without END is interrupted after reboot");

    MemoryFlash atomic_flash{3U};
    CircularHistoryLog atomic_log{atomic_flash};
    context.require(atomic_log.initialize(), "atomic eviction flash initializes");
    const auto atomic_first = atomic_log.begin_session(
        observation(HistoryObservationKind::Start, 0)
    );
    context.require(atomic_first.has_value(), "atomic eviction victim starts");
    std::int64_t victim_samples = 0;
    while (atomic_flash.erase_calls() < 2U && victim_samples < 100) {
        ++victim_samples;
        context.require(atomic_log.append(observation(
                            HistoryObservationKind::Sample,
                            victim_samples * 60'000
                        )), "atomic eviction victim reaches a second page");
    }
    auto atomic_end = observation(
        HistoryObservationKind::End,
        (victim_samples + 1) * 60'000,
        SessionStatus::Stopped
    );
    atomic_end.stop_reason = smoker::core::StopReason::User;
    context.require(atomic_flash.erase_calls() == 2U && atomic_log.append(atomic_end),
                    "atomic eviction victim completes across exactly two pages");
    const auto atomic_second = atomic_log.begin_session(
        observation(HistoryObservationKind::Start, 0)
    );
    context.require(atomic_second.has_value() && atomic_log.contains_session(*atomic_first),
                    "new session consumes the last free page before eviction");

    // Commit the victim tombstone, erase one of its pages, then fail the erase
    // of the marker page. A reboot at this point must never expose the
    // remaining physical page as a truncated/interrupted old session.
    atomic_flash.fail_next_erase_after(1U, true);
    bool interrupted_eviction = false;
    for (std::int64_t index = 1; index <= 100; ++index) {
        if (!atomic_log.append(observation(
                HistoryObservationKind::Sample, index * 60'000
            ))) {
            interrupted_eviction = true;
            break;
        }
    }
    context.require(interrupted_eviction,
                    "injected erase failure interrupts a multi-page eviction");
    context.require(!atomic_log.contains_session(*atomic_first),
                    "committed eviction hides the whole victim before cleanup finishes");
    const auto during_failure = atomic_log.sessions(std::nullopt, 8U);
    context.require(std::none_of(
                        during_failure.begin(), during_failure.end(),
                        [atomic_first](const auto& summary) {
                            return summary.history_id == *atomic_first;
                        }
                    ), "query never exposes a partial victim after erase failure");

    CircularHistoryLog after_eviction_reset{atomic_flash};
    context.require(after_eviction_reset.initialize(),
                    "reboot completes an interrupted eviction from its partial tombstone");
    const auto after_reset = after_eviction_reset.sessions(std::nullopt, 8U);
    context.require(std::none_of(
                        after_reset.begin(), after_reset.end(),
                        [atomic_first](const auto& summary) {
                            return summary.history_id == *atomic_first;
                        }
                    ), "reboot never reconstructs a partially erased completed session");
    context.require(std::any_of(
                        after_reset.begin(), after_reset.end(),
                        [atomic_second](const auto& summary) {
                            return summary.history_id == *atomic_second;
                        }
                    ), "interrupted eviction preserves the newer session");
}

SmokerSnapshotView snapshot(
    const SessionStatus status,
    const std::uint32_t id,
    const std::int64_t elapsed,
    ProbeSnapshotView& probe,
    std::span<const smoker::core::Alarm> alarms = {}
)
{
    return SmokerSnapshotView{
        status,
        id == 0U ? std::nullopt : std::optional<smoker::core::SessionId>{id},
        Duration{elapsed},
        status == SessionStatus::Stopped ? smoker::core::StopReason::User
                                         : smoker::core::StopReason::None,
        temperature(80.0F),
        temperature(110.0F),
        *smoker::core::HeaterDemand::from_percent(50.0F),
        {},
        std::span<const ProbeSnapshotView>{&probe, 1U},
        alarms,
        std::nullopt,
        false,
        temperature(150.0F),
        0U,
        {},
    };
}

void test_sampling_and_mailbox_saturation(TestContext& context)
{
    HistoryObservationMailbox mailbox{1U, 3U};
    ProbeSnapshotView probe{
        1U, "Aliment", smoker::core::ProbeRole::Meat,
        temperature(60.0F), temperature(75.0F), true, true
    };
    mailbox.observe(snapshot(SessionStatus::Idle, 0U, 0, probe));
    allocation_probe::begin();
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 0, probe));
    const auto publication_allocations = allocation_probe::end();
    context.require(publication_allocations == 0U,
                    "ControlTask history publication performs no ordinary C++ allocation");
    HistoryObservation record;
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Start,
                    "RUNNING transition publishes START immediately");
    probe.current_temperature = temperature(61.0F);
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 59'000, probe));
    context.require(!mailbox.try_pop(record), "readings alone do not create high-frequency records");
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 60'000, probe));
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Sample,
                    "RUNNING publishes at sixty seconds");
    probe.target_temperature = temperature(76.0F);
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 120'000, probe));
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Change
                        && record.probes.front().target_temperature == temperature(76.0F),
                    "semantic changes publish immediately at a sample boundary");
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Sample,
                    "a semantic change does not replace the due periodic sample");
    probe.target_temperature = temperature(77.0F);
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 121'000, probe));
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Change
                        && record.probes.front().target_temperature == temperature(77.0F),
                    "each later semantic transition is also immediate");
    mailbox.observe(snapshot(SessionStatus::Running, 9U, 180'000, probe));
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::Sample,
                    "semantic changes do not postpone the sixty-second sample cadence");
    mailbox.observe(snapshot(SessionStatus::Stopped, 9U, 121'000, probe));
    context.require(mailbox.try_pop(record) && record.kind == HistoryObservationKind::End,
                    "terminal lifecycle publishes one complete END record");
    context.require(!mailbox.try_pop(record), "END does not spend space on redundant CHANGE");

    HistoryObservationMailbox direct_terminal{1U, 3U};
    direct_terminal.observe(snapshot(SessionStatus::Idle, 0U, 0, probe));
    direct_terminal.observe(snapshot(SessionStatus::Stopped, 20U, 1, probe));
    context.require(
        direct_terminal.try_pop(record)
            && record.kind == HistoryObservationKind::Start
            && record.application_session_id == 20U,
        "a session terminal in its first observed cycle still publishes START"
    );
    context.require(
        direct_terminal.try_pop(record)
            && record.kind == HistoryObservationKind::End
            && record.application_session_id == 20U,
        "a session terminal in its first observed cycle publishes END after START"
    );

    HistoryObservationMailbox paired_retry{1U, 3U};
    paired_retry.observe(snapshot(SessionStatus::Idle, 0U, 0, probe));
    paired_retry.observe(snapshot(SessionStatus::Running, 30U, 0, probe));
    for (std::uint32_t index = 0U; index < 14U; ++index) {
        probe.target_temperature = temperature(60.0F + static_cast<float>(index));
        paired_retry.observe(snapshot(
            SessionStatus::Running, 30U, static_cast<std::int64_t>(index + 1U), probe
        ));
    }
    paired_retry.observe(snapshot(SessionStatus::Fault, 31U, 1, probe));
    std::size_t premature_new_session_records = 0U;
    while (paired_retry.try_pop(record)) {
        if (record.application_session_id == 31U) ++premature_new_session_records;
    }
    context.require(
        premature_new_session_records == 0U,
        "a saturated mailbox never publishes half of a direct terminal lifecycle pair"
    );
    paired_retry.observe(snapshot(SessionStatus::Fault, 31U, 1, probe));
    context.require(
        paired_retry.try_pop(record) && record.kind == HistoryObservationKind::Start,
        "a deferred direct terminal lifecycle retries START after capacity returns"
    );
    context.require(
        paired_retry.try_pop(record) && record.kind == HistoryObservationKind::End,
        "a deferred direct terminal lifecycle retries END with START"
    );

    HistoryObservationMailbox reserved{1U, 3U};
    reserved.observe(snapshot(SessionStatus::Idle, 0U, 0, probe));
    reserved.observe(snapshot(SessionStatus::Running, 10U, 0, probe));
    for (std::uint32_t index = 0U; index < 15U; ++index) {
        probe.target_temperature = temperature(80.0F + static_cast<float>(index));
        const auto changed_at = 10'000LL + static_cast<std::int64_t>(index) * 10'000LL;
        reserved.observe(snapshot(SessionStatus::Running, 10U, changed_at, probe));
        reserved.observe(snapshot(SessionStatus::Running, 10U, changed_at + 5'000, probe));
    }
    context.require(reserved.dropped_count() > 0U,
                    "ordinary history overflow drops without waiting");
    reserved.observe(snapshot(SessionStatus::Stopped, 10U, 170'000, probe));
    std::size_t end_records = 0U;
    while (reserved.try_pop(record)) {
        if (record.kind == HistoryObservationKind::End) ++end_records;
    }
    context.require(end_records == 1U,
                    "ordinary saturation preserves one terminal END admission");
}

void test_control_output_is_independent_of_history_saturation(TestContext& context)
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
        chamber,
        probe_source,
        heater,
        clock,
        events,
        smoker::core::SafetyLimits{temperature(150.0F)},
        probes,
    };
    HistoryObservationMailbox mailbox{probes.size(), (probes.size() * 2U) + 1U};
    mailbox.observe(application.snapshot_view());

    smoker::core::Recipe recipe{
        1U,
        "M14 saturation",
        smoker::core::Stage{1U, "Single stage", temperature(110.0F), std::nullopt},
    };
    context.require(
        application.submit(smoker::app::StartSessionCommand{1U, recipe}),
        "control/history isolation session is admitted"
    );
    application.tick();
    mailbox.observe(application.snapshot_view());
    context.require(
        heater.last_demand().percent() == 100.0F,
        "control heats normally before history saturation"
    );

    for (std::uint32_t index = 0U; index < 24U; ++index) {
        const auto target = temperature(100.0F + static_cast<float>(index % 15U));
        context.require(
            application.submit(smoker::app::SetChamberTargetCommand{target}),
            "target change is admitted while saturating history"
        );
        clock.advance(Duration{1'000});
        application.tick();
        mailbox.observe(application.snapshot_view());
        context.require(
            heater.last_demand().percent() == 100.0F,
            "history saturation does not suppress the safety-gated heater write"
        );
    }
    context.require(
        mailbox.dropped_count() > 0U,
        "integrated control test actually saturates history publication"
    );

    chamber.set_reading(temperature(130.0F));
    clock.advance(Duration{1'000});
    application.tick();
    mailbox.observe(application.snapshot_view());
    context.require(
        heater.last_demand().percent() == 0.0F,
        "control still turns heater off from chamber state while history is saturated"
    );
}

void test_flash_operation_serialization(TestContext& context)
{
    smoker::platform::FlashOperationCoordinator coordinator;
    context.require(coordinator.try_acquire_history(), "history acquires idle flash owner");
    context.require(!coordinator.try_acquire_ota(),
                    "OTA defers and waits for bounded in-progress history write");
    context.require(coordinator.history_deferred(), "OTA admission defers new history work");
    coordinator.release_history();
    context.require(coordinator.try_acquire_ota(), "OTA acquires after history releases");
    context.require(!coordinator.try_acquire_history(), "history never overlaps OTA flash work");
    coordinator.release_ota();
    context.require(coordinator.try_acquire_history(), "history resumes after OTA release");
    coordinator.release_history();
}

void test_mailbox_concurrency(TestContext& context)
{
    HistoryObservationMailbox mailbox{1U, 1U};
    ProbeSnapshotView probe{
        1U, "Aliment", smoker::core::ProbeRole::Meat,
        temperature(50.0F), temperature(75.0F), true, true
    };
    std::atomic_bool producer_done{false};
    std::atomic<std::uint32_t> consumed{0U};
    std::thread producer{[&] {
        mailbox.observe(snapshot(SessionStatus::Idle, 0U, 0, probe));
        for (std::uint32_t id = 1U; id <= 2'000U; ++id) {
            mailbox.observe(snapshot(SessionStatus::Running, id, 0, probe));
            mailbox.observe(snapshot(SessionStatus::Stopped, id, 1, probe));
        }
        producer_done.store(true, std::memory_order_release);
    }};
    std::thread consumer{[&] {
        HistoryObservation value;
        while (true) {
            if (mailbox.try_pop(value)) {
                consumed.fetch_add(1U, std::memory_order_relaxed);
            } else if (producer_done.load(std::memory_order_acquire)) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    }};
    producer.join();
    consumer.join();
    context.require(consumed.load(std::memory_order_relaxed) > 0U,
                    "concurrent SPSC history transport makes progress");
}

void test_strict_history_queries(TestContext& context)
{
    using smoker::platform::parse_history_samples_query;
    using smoker::platform::parse_history_sessions_query;
    const auto sessions = parse_history_sessions_query("before=42&limit=32");
    context.require(sessions && sessions->before == 42U && sessions->limit == 32U,
                    "session query accepts bounded canonical parameters");
    context.require(!parse_history_sessions_query("limit=1&limit=2"),
                    "session query rejects duplicate parameters");
    context.require(!parse_history_sessions_query("limit=33")
                        && !parse_history_sessions_query("unknown=1")
                        && !parse_history_sessions_query("before=-1"),
                    "session query rejects range, unknown, and malformed values");
    const auto samples = parse_history_samples_query(
        "history_id=18446744073709551615&after=0&limit=60&stride=65535"
    );
    context.require(samples && samples->after == 0U && samples->limit == 60U
                        && samples->stride == 65535U,
                    "sample query accepts full durable id and bounded pagination");
    context.require(!parse_history_samples_query("after=0")
                        && !parse_history_samples_query("history_id=0")
                        && !parse_history_samples_query("history_id=1&stride=0")
                        && !parse_history_samples_query("history_id=1&x=2")
                        && !parse_history_samples_query("history_id=1&history_id=2"),
                    "sample query strictly requires one valid id and known ranges");
}

} // namespace

int main()
{
    TestContext context;
    test_empty_random_and_reboot(context);
    test_torn_and_corrupt_records(context);
    test_pagination_and_stride(context);
    test_rollover_eviction_truncation_and_interruption(context);
    test_sampling_and_mailbox_saturation(context);
    test_control_output_is_independent_of_history_saturation(context);
    test_flash_operation_serialization(context);
    test_mailbox_concurrency(context);
    test_strict_history_queries(context);
    if (context.failures != 0) {
        std::fprintf(stderr, "%d M14 test(s) failed\n", context.failures);
        return 1;
    }
    std::puts("M14 history tests passed");
    return 0;
}

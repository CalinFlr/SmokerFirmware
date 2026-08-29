#pragma once

#include "smoker/app/snapshot_view.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace smoker::platform {

inline constexpr std::size_t history_mailbox_capacity = 16U;
inline constexpr std::size_t history_page_bytes = 4096U;
inline constexpr std::size_t history_partition_bytes = 4U * 1024U * 1024U;
inline constexpr core::Duration history_periodic_sample_interval{60'000};

enum class HistoryObservationKind : std::uint8_t { Start, Sample, Change, End };
enum class HistoryStorageState : std::uint8_t { Ready, Degraded, Failed };

struct HistoryProbe final {
    core::ProbeId id{};
    core::ProbeRole role{core::ProbeRole::Unassigned};
    std::optional<core::Temperature> current_temperature;
    std::optional<core::Temperature> target_temperature;
    bool enabled{false};
    bool alarm_enabled{false};
};

struct HistoryAlarm final {
    core::AlarmId id{};
    core::AlarmCode code{core::AlarmCode::TimerCompleted};
    std::optional<core::ProbeId> probe_id;
    bool acknowledged{false};
};

struct HistoryObservation final {
    HistoryObservationKind kind{HistoryObservationKind::Sample};
    std::uint64_t history_id{0U};
    std::uint32_t sequence{0U};
    core::SessionId application_session_id{0U};
    core::SessionStatus session_status{core::SessionStatus::Idle};
    core::StopReason stop_reason{core::StopReason::None};
    core::Duration session_elapsed{};
    std::optional<std::int64_t> unix_utc_seconds;
    std::optional<core::Temperature> chamber_temperature;
    std::optional<core::Temperature> chamber_target;
    core::HeaterDemand heater_demand{core::HeaterDemand::off()};
    core::TimerRuntimeState timer;
    std::vector<HistoryProbe> probes;
    std::vector<HistoryAlarm> alarms;
    std::optional<core::FaultCode> fault;
};

struct HistorySessionSummary final {
    std::uint64_t history_id{0U};
    core::SessionId application_session_id{0U};
    std::optional<std::int64_t> start_unix_utc_seconds;
    std::optional<std::int64_t> end_unix_utc_seconds;
    core::Duration elapsed{};
    std::uint32_t sample_count{0U};
    core::SessionStatus final_status{core::SessionStatus::Running};
    core::StopReason stop_reason{core::StopReason::None};
    bool active{false};
    bool interrupted{false};
    bool truncated{false};
};

struct HistoryHealth final {
    HistoryStorageState state{HistoryStorageState::Ready};
    std::uint64_t mailbox_drops{0U};
    std::uint64_t corrupt_records{0U};
    std::uint64_t write_errors{0U};
    std::size_t capacity_bytes{0U};
    std::size_t used_bytes{0U};
};

class HistoryObservationMailbox final {
public:
    HistoryObservationMailbox(std::size_t probe_capacity, std::size_t alarm_capacity);

    // ControlTask-only producer. Bounded, wait-free after construction, and
    // allocation-free. A terminal cycle emits one complete END observation;
    // a full mailbox drops ordinary observations instead of waiting.
    void observe(
        const app::SmokerSnapshotView& snapshot,
        std::optional<std::int64_t> unix_utc_seconds = std::nullopt
    ) noexcept;
    [[nodiscard]] bool try_pop(HistoryObservation& destination) noexcept;
    [[nodiscard]] std::uint64_t dropped_count() const noexcept;

private:
    struct Slot final {
        HistoryObservation observation;
    };

    [[nodiscard]] bool changed(const app::SmokerSnapshotView& snapshot) const noexcept;
    [[nodiscard]] bool has_capacity(std::size_t count) const noexcept;
    void remember(const app::SmokerSnapshotView& snapshot) noexcept;
    [[nodiscard]] bool push(
        HistoryObservationKind kind,
        const app::SmokerSnapshotView& snapshot,
        std::optional<std::int64_t> unix_utc_seconds
    ) noexcept;

    std::array<Slot, history_mailbox_capacity> slots_{};
    std::vector<HistoryProbe> previous_probes_;
    std::vector<HistoryAlarm> previous_alarms_;
    std::optional<core::SessionId> previous_session_id_;
    core::SessionStatus previous_status_{core::SessionStatus::Idle};
    std::optional<core::Temperature> previous_target_;
    core::TimerRuntimeState previous_timer_;
    std::optional<core::FaultCode> previous_fault_;
    core::Duration last_sample_elapsed_{};
    bool initialized_{false};
    std::atomic<std::uint32_t> write_sequence_{0U};
    std::atomic<std::uint32_t> read_sequence_{0U};
    // Native-width on ESP32-S3; the ControlTask path must not require a
    // global-lock-backed 64-bit atomic helper.
    std::atomic<std::uint32_t> dropped_count_{0U};
};

class IHistoryFlash {
public:
    virtual ~IHistoryFlash() = default;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t sector_size() const noexcept = 0;
    [[nodiscard]] virtual bool read(
        std::size_t offset, std::span<std::uint8_t> destination
    ) const noexcept = 0;
    [[nodiscard]] virtual bool write(
        std::size_t offset, std::span<const std::uint8_t> source
    ) noexcept = 0;
    [[nodiscard]] virtual bool erase_sector(std::size_t offset) noexcept = 0;
};

class CircularHistoryLog final {
public:
    explicit CircularHistoryLog(IHistoryFlash& flash);

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] std::optional<std::uint64_t> begin_session(
        HistoryObservation observation
    ) noexcept;
    [[nodiscard]] bool append(HistoryObservation observation) noexcept;
    void set_mailbox_drops(std::uint64_t drops) noexcept;
    [[nodiscard]] HistoryHealth health() const noexcept;
    [[nodiscard]] bool contains_session(std::uint64_t history_id) const noexcept;

    [[nodiscard]] std::vector<HistorySessionSummary> sessions(
        std::optional<std::uint64_t> before, std::size_t limit
    );
    [[nodiscard]] std::vector<HistoryObservation> samples(
        std::uint64_t history_id,
        std::optional<std::uint32_t> after,
        std::size_t limit,
        std::uint16_t stride,
        std::optional<std::uint32_t>& continuation
    );
    [[nodiscard]] bool last_query_failed() const noexcept;

private:
    struct PageInfo final {
        std::size_t offset{0U};
        std::uint64_t generation{0U};
        std::uint64_t history_id{0U};
        std::uint32_t page_index{0U};
        std::size_t used{0U};
        bool valid{false};
        bool eviction_marker{false};
        bool eviction_marker_writable{true};
    };

    [[nodiscard]] bool scan_page(std::size_t page_index, PageInfo& page) noexcept;
    [[nodiscard]] bool allocate_page(std::uint64_t history_id, bool session_start) noexcept;
    [[nodiscard]] bool write_page_header(PageInfo& page) noexcept;
    [[nodiscard]] bool append_assigned(HistoryObservation& observation) noexcept;
    [[nodiscard]] bool read_page_records(
        const PageInfo& page, std::vector<HistoryObservation>& destination
    );
    [[nodiscard]] std::optional<std::size_t> select_page_for_reuse(
        std::uint64_t history_id
    ) noexcept;
    [[nodiscard]] bool mark_session_for_eviction(std::size_t page_index) noexcept;
    [[nodiscard]] std::optional<std::size_t> complete_pending_eviction(
        std::uint64_t history_id, bool keep_marker_for_reuse
    ) noexcept;
    [[nodiscard]] bool session_is_pending_eviction(
        std::uint64_t history_id
    ) const noexcept;
    void note_corrupt_record() noexcept;
    void note_write_error() noexcept;

    IHistoryFlash& flash_;
    std::vector<PageInfo> pages_;
    std::optional<std::size_t> current_page_;
    std::optional<std::uint64_t> active_history_id_;
    std::uint64_t next_history_id_{1U};
    std::uint64_t next_generation_{1U};
    std::uint32_t next_sequence_{0U};
    std::uint64_t mailbox_drops_{0U};
    std::uint64_t corrupt_records_{0U};
    std::uint64_t write_errors_{0U};
    std::uint32_t consecutive_write_errors_{0U};
    bool degraded_{false};
    bool last_query_failed_{false};
};

enum class HistoryWriteCycleResult : std::uint8_t {
    NoObservation,
    Written,
    RetryLifecycle,
    DroppedOrdinary,
    TerminalFailStop,
    Stopped,
};

struct HistoryWriteAttemptResult final {
    bool written{false};
    HistoryStorageState storage_state{HistoryStorageState::Ready};
};

// HistoryTask-only policy. It keeps failed lifecycle observations ahead of
// later mailbox entries, but only while CircularHistoryLog remains retryable.
// Once the log reports Failed, process() permanently refuses to invoke the
// supplied writer.
class HistoryWritePolicy final {
public:
    [[nodiscard]] bool terminal() const noexcept { return terminal_; }
    [[nodiscard]] bool has_pending_lifecycle() const noexcept
    {
        return pending_lifecycle_.has_value();
    }

    template <typename Writer>
    [[nodiscard]] HistoryWriteCycleResult process(
        std::optional<HistoryObservation> observation, Writer&& writer
    )
    {
        if (terminal_) return HistoryWriteCycleResult::Stopped;

        HistoryObservation* selected = nullptr;
        if (pending_lifecycle_) {
            selected = &*pending_lifecycle_;
        } else if (observation) {
            selected = &*observation;
        } else {
            return HistoryWriteCycleResult::NoObservation;
        }

        const auto attempt = std::forward<Writer>(writer)(*selected);
        if (attempt.written) {
            pending_lifecycle_.reset();
            return HistoryWriteCycleResult::Written;
        }
        if (attempt.storage_state == HistoryStorageState::Failed) {
            pending_lifecycle_.reset();
            terminal_ = true;
            return HistoryWriteCycleResult::TerminalFailStop;
        }
        if (selected->kind == HistoryObservationKind::Start
            || selected->kind == HistoryObservationKind::End) {
            if (!pending_lifecycle_) pending_lifecycle_ = std::move(*selected);
            return HistoryWriteCycleResult::RetryLifecycle;
        }
        return HistoryWriteCycleResult::DroppedOrdinary;
    }

private:
    std::optional<HistoryObservation> pending_lifecycle_;
    bool terminal_{false};
};

} // namespace smoker::platform

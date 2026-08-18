#include "smoker/platform/history_support.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace smoker::platform {
namespace {

constexpr std::uint32_t page_magic = 0x47505348U; // HSPG, little endian
constexpr std::uint32_t record_magic = 0x43455248U; // HREC, little endian
constexpr std::uint16_t format_version = 1U;
constexpr std::uint32_t commit_marker = 0x54494D43U; // CMIT
constexpr std::uint32_t eviction_marker = 0x54435645U; // EVCT
constexpr std::size_t page_header_bytes = 40U;
constexpr std::size_t record_header_bytes = 40U;
constexpr std::uint32_t failed_write_threshold = 3U;

bool is_eviction_marker_state(const std::uint32_t stored) noexcept
{
    // Programming clears bits and erasing restores them. Treat every
    // non-erased word reachable between EVCT and 0xFFFFFFFF as a tombstone.
    // This fail-closed rule covers reset during either marker programming or
    // the final marker-sector erase without exposing a partial victim.
    return stored != std::numeric_limits<std::uint32_t>::max()
        && (stored & eviction_marker) == eviction_marker;
}

std::uint32_t crc32(const std::span<const std::uint8_t> bytes) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

template <typename Integer>
void put(std::span<std::uint8_t> bytes, const std::size_t offset, Integer value) noexcept
{
    using Unsigned = std::make_unsigned_t<Integer>;
    auto encoded = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(encoded & 0xFFU);
        if constexpr (sizeof(Integer) > 1U) {
            encoded >>= 8U;
        }
    }
}

template <typename Integer>
Integer get(const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept
{
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned value = 0U;
    for (std::size_t index = 0U; index < sizeof(Integer); ++index) {
        value = static_cast<Unsigned>(
            value | static_cast<Unsigned>(
                static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U)
            )
        );
    }
    return static_cast<Integer>(value);
}

template <typename Integer>
void append_integer(std::vector<std::uint8_t>& bytes, Integer value)
{
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(Integer));
    put<Integer>(bytes, offset, value);
}

void append_float(std::vector<std::uint8_t>& bytes, const float value)
{
    append_integer(bytes, std::bit_cast<std::uint32_t>(value));
}

template <typename Integer>
bool take(const std::span<const std::uint8_t> bytes, std::size_t& cursor, Integer& value) noexcept
{
    if (cursor + sizeof(Integer) > bytes.size()) {
        return false;
    }
    value = get<Integer>(bytes, cursor);
    cursor += sizeof(Integer);
    return true;
}

bool take_float(
    const std::span<const std::uint8_t> bytes, std::size_t& cursor, float& value
) noexcept
{
    std::uint32_t encoded = 0U;
    if (!take(bytes, cursor, encoded)) {
        return false;
    }
    value = std::bit_cast<float>(encoded);
    return true;
}

std::vector<std::uint8_t> encode_observation(const HistoryObservation& value)
{
    std::vector<std::uint8_t> result;
    result.reserve(64U + value.probes.size() * 12U + value.alarms.size() * 8U);
    append_integer(result, std::uint16_t{1U});
    append_integer(result, static_cast<std::uint8_t>(value.session_status));
    append_integer(result, static_cast<std::uint8_t>(value.stop_reason));
    append_integer(result, value.application_session_id);
    append_integer(result, static_cast<std::int64_t>(value.session_elapsed.count()));
    std::uint8_t flags = 0U;
    if (value.unix_utc_seconds) flags |= std::uint8_t{0x01U};
    if (value.chamber_temperature) flags |= std::uint8_t{0x02U};
    if (value.chamber_target) flags |= std::uint8_t{0x04U};
    if (value.timer.started) flags |= std::uint8_t{0x08U};
    if (value.timer.completed) flags |= std::uint8_t{0x10U};
    if (value.fault) flags |= std::uint8_t{0x20U};
    append_integer(result, flags);
    append_integer(result, value.unix_utc_seconds.value_or(0));
    append_float(result, value.chamber_temperature
        ? value.chamber_temperature->celsius() : 0.0F);
    append_float(result, value.chamber_target ? value.chamber_target->celsius() : 0.0F);
    append_float(result, value.heater_demand.percent());
    append_integer(result, static_cast<std::int64_t>(value.timer.elapsed.count()));
    append_integer(result, static_cast<std::uint8_t>(value.fault.value_or(
        core::FaultCode::ConfigurationInvalid
    )));
    append_integer(result, static_cast<std::uint16_t>(value.probes.size()));
    append_integer(result, static_cast<std::uint16_t>(value.alarms.size()));
    for (const auto& probe : value.probes) {
        append_integer(result, probe.id);
        append_integer(result, static_cast<std::uint8_t>(probe.role));
        std::uint8_t probe_flags = 0U;
        if (probe.enabled) probe_flags |= std::uint8_t{0x01U};
        if (probe.alarm_enabled) probe_flags |= std::uint8_t{0x02U};
        if (probe.current_temperature) probe_flags |= std::uint8_t{0x04U};
        if (probe.target_temperature) probe_flags |= std::uint8_t{0x08U};
        append_integer(result, probe_flags);
        append_integer(result, std::uint8_t{0U});
        append_float(result, probe.current_temperature
            ? probe.current_temperature->celsius() : 0.0F);
        append_float(result, probe.target_temperature
            ? probe.target_temperature->celsius() : 0.0F);
    }
    for (const auto& alarm : value.alarms) {
        append_integer(result, alarm.id);
        append_integer(result, static_cast<std::uint8_t>(alarm.code));
        std::uint8_t alarm_flags = alarm.acknowledged ? std::uint8_t{0x01U}
                                                     : std::uint8_t{0U};
        if (alarm.probe_id) alarm_flags |= std::uint8_t{0x02U};
        append_integer(result, alarm_flags);
        append_integer(result, alarm.probe_id.value_or(0U));
        append_integer(result, std::uint8_t{0U});
    }
    return result;
}

bool decode_observation(
    const std::span<const std::uint8_t> bytes, HistoryObservation& result
)
{
    std::size_t cursor = 0U;
    std::uint16_t version = 0U;
    std::uint8_t status = 0U;
    std::uint8_t stop_reason = 0U;
    std::int64_t elapsed = 0;
    std::uint8_t flags = 0U;
    std::int64_t utc = 0;
    float chamber = 0.0F;
    float target = 0.0F;
    float demand = 0.0F;
    std::int64_t timer_elapsed = 0;
    std::uint8_t fault = 0U;
    std::uint16_t probe_count = 0U;
    std::uint16_t alarm_count = 0U;
    if (!take(bytes, cursor, version) || version != 1U
        || !take(bytes, cursor, status)
        || status > static_cast<std::uint8_t>(core::SessionStatus::Fault)
        || !take(bytes, cursor, stop_reason)
        || stop_reason > static_cast<std::uint8_t>(core::StopReason::RecoveryNotAllowed)
        || !take(bytes, cursor, result.application_session_id)
        || !take(bytes, cursor, elapsed) || elapsed < 0
        || !take(bytes, cursor, flags)
        || !take(bytes, cursor, utc)
        || !take_float(bytes, cursor, chamber)
        || !take_float(bytes, cursor, target)
        || !take_float(bytes, cursor, demand)
        || !take(bytes, cursor, timer_elapsed) || timer_elapsed < 0
        || !take(bytes, cursor, fault)
        || !take(bytes, cursor, probe_count)
        || !take(bytes, cursor, alarm_count)) {
        return false;
    }
    const auto demand_value = core::HeaterDemand::from_percent(demand);
    if (!demand_value || probe_count > 256U || alarm_count > 256U) {
        return false;
    }
    result.session_status = static_cast<core::SessionStatus>(status);
    result.stop_reason = static_cast<core::StopReason>(stop_reason);
    result.session_elapsed = core::Duration{elapsed};
    result.unix_utc_seconds = (flags & 0x01U) != 0U
        ? std::optional<std::int64_t>{utc} : std::nullopt;
    if ((flags & 0x02U) != 0U) {
        result.chamber_temperature = core::Temperature::from_celsius(chamber);
        if (!result.chamber_temperature) return false;
    } else {
        result.chamber_temperature.reset();
    }
    if ((flags & 0x04U) != 0U) {
        result.chamber_target = core::Temperature::from_celsius(target);
        if (!result.chamber_target) return false;
    } else {
        result.chamber_target.reset();
    }
    result.heater_demand = *demand_value;
    result.timer.started = (flags & 0x08U) != 0U;
    result.timer.completed = (flags & 0x10U) != 0U;
    result.timer.started_at.reset();
    result.timer.elapsed = core::Duration{timer_elapsed};
    if ((flags & 0x20U) != 0U) {
        if (fault > static_cast<std::uint8_t>(core::FaultCode::ConfigurationInvalid)) {
            return false;
        }
        result.fault = static_cast<core::FaultCode>(fault);
    } else {
        result.fault.reset();
    }
    result.probes.clear();
    result.alarms.clear();
    result.probes.reserve(probe_count);
    result.alarms.reserve(alarm_count);
    for (std::uint16_t index = 0U; index < probe_count; ++index) {
        HistoryProbe probe;
        std::uint8_t role = 0U;
        std::uint8_t probe_flags = 0U;
        std::uint8_t reserved = 0U;
        float current = 0.0F;
        float probe_target = 0.0F;
        if (!take(bytes, cursor, probe.id) || !take(bytes, cursor, role)
            || role > static_cast<std::uint8_t>(core::ProbeRole::Unassigned)
            || !take(bytes, cursor, probe_flags)
            || !take(bytes, cursor, reserved)
            || !take_float(bytes, cursor, current)
            || !take_float(bytes, cursor, probe_target)) {
            return false;
        }
        probe.role = static_cast<core::ProbeRole>(role);
        probe.enabled = (probe_flags & 0x01U) != 0U;
        probe.alarm_enabled = (probe_flags & 0x02U) != 0U;
        if ((probe_flags & 0x04U) != 0U) {
            probe.current_temperature = core::Temperature::from_celsius(current);
            if (!probe.current_temperature) return false;
        }
        if ((probe_flags & 0x08U) != 0U) {
            probe.target_temperature = core::Temperature::from_celsius(probe_target);
            if (!probe.target_temperature) return false;
        }
        result.probes.push_back(probe);
    }
    for (std::uint16_t index = 0U; index < alarm_count; ++index) {
        HistoryAlarm alarm;
        std::uint8_t code = 0U;
        std::uint8_t alarm_flags = 0U;
        core::ProbeId probe_id = 0U;
        std::uint8_t reserved = 0U;
        if (!take(bytes, cursor, alarm.id) || !take(bytes, cursor, code)
            || code > static_cast<std::uint8_t>(core::AlarmCode::TimerCompleted)
            || !take(bytes, cursor, alarm_flags)
            || !take(bytes, cursor, probe_id)
            || !take(bytes, cursor, reserved)) {
            return false;
        }
        alarm.code = static_cast<core::AlarmCode>(code);
        alarm.acknowledged = (alarm_flags & 0x01U) != 0U;
        if ((alarm_flags & 0x02U) != 0U) alarm.probe_id = probe_id;
        result.alarms.push_back(alarm);
    }
    return cursor == bytes.size();
}

bool probes_equal(
    const std::span<const app::ProbeSnapshotView> current,
    const std::vector<HistoryProbe>& previous
) noexcept
{
    if (current.size() != previous.size()) return false;
    for (std::size_t index = 0U; index < current.size(); ++index) {
        const auto& left = current[index];
        const auto& right = previous[index];
        if (left.id != right.id || left.role != right.role
            || left.target_temperature != right.target_temperature
            || left.enabled != right.enabled
            || left.alarm_enabled != right.alarm_enabled) return false;
    }
    return true;
}

bool alarms_equal(
    const std::span<const core::Alarm> current,
    const std::vector<HistoryAlarm>& previous
) noexcept
{
    if (current.size() != previous.size()) return false;
    for (std::size_t index = 0U; index < current.size(); ++index) {
        const auto& left = current[index];
        const auto& right = previous[index];
        if (left.id != right.id || left.code != right.code
            || left.probe_id != right.probe_id
            || left.acknowledged != right.acknowledged) return false;
    }
    return true;
}

} // namespace

HistoryObservationMailbox::HistoryObservationMailbox(
    const std::size_t probe_capacity, const std::size_t alarm_capacity
)
{
    previous_probes_.reserve(probe_capacity);
    previous_alarms_.reserve(alarm_capacity);
    for (auto& slot : slots_) {
        slot.observation.probes.reserve(probe_capacity);
        slot.observation.alarms.reserve(alarm_capacity);
    }
}

void HistoryObservationMailbox::observe(
    const app::SmokerSnapshotView& snapshot,
    const std::optional<std::int64_t> unix_utc_seconds
) noexcept
{
    const bool running = snapshot.session_status == core::SessionStatus::Running
        && snapshot.session_id.has_value();
    const bool starting = running
        && (!initialized_ || previous_status_ != core::SessionStatus::Running
            || previous_session_id_ != snapshot.session_id);
    const bool ending = initialized_
        && previous_status_ == core::SessionStatus::Running
        && previous_session_id_ == snapshot.session_id && !running;
    const bool terminal_session_first_seen = snapshot.session_id.has_value()
        && (snapshot.session_status == core::SessionStatus::Stopped
            || snapshot.session_status == core::SessionStatus::Fault)
        && (!initialized_ || previous_session_id_ != snapshot.session_id);
    const bool state_changed = initialized_ && changed(snapshot);

    if (starting) {
        if (!push(HistoryObservationKind::Start, snapshot, unix_utc_seconds)) return;
        last_sample_elapsed_ = snapshot.session_elapsed;
    } else if (ending) {
        // END already contains the complete terminal state. Retry it on later
        // cycles when the mailbox is full instead of spending the last slot on
        // a redundant CHANGE and making a completed session look interrupted.
        if (!push(HistoryObservationKind::End, snapshot, unix_utc_seconds)) return;
    } else if (terminal_session_first_seen) {
        // Start+Stop or Start+FAULT can happen inside one application tick, so
        // the post-control projection first sees the session in a terminal
        // state. Admit the lifecycle pair atomically from the producer's point
        // of view; if two slots are unavailable, retry the same snapshot later
        // without publishing a duplicate START.
        if (!has_capacity(2U)) {
            dropped_count_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (!push(HistoryObservationKind::Start, snapshot, unix_utc_seconds)
            || !push(HistoryObservationKind::End, snapshot, unix_utc_seconds)) {
            return;
        }
    } else if (running) {
        const bool periodic_due = snapshot.session_elapsed >= last_sample_elapsed_
            && snapshot.session_elapsed - last_sample_elapsed_
                >= history_periodic_sample_interval;
        if (state_changed) {
            static_cast<void>(push(
                HistoryObservationKind::Change, snapshot, unix_utc_seconds
            ));
        }
        if (periodic_due
            && push(HistoryObservationKind::Sample, snapshot, unix_utc_seconds)) {
            last_sample_elapsed_ = snapshot.session_elapsed;
        }
    }
    remember(snapshot);
    initialized_ = true;
}

bool HistoryObservationMailbox::has_capacity(const std::size_t count) const noexcept
{
    const auto write = write_sequence_.load(std::memory_order_relaxed);
    const auto read = read_sequence_.load(std::memory_order_acquire);
    const auto pending = write - read;
    return count <= history_mailbox_capacity
        && pending <= history_mailbox_capacity - count;
}

bool HistoryObservationMailbox::try_pop(HistoryObservation& destination) noexcept
{
    const auto read = read_sequence_.load(std::memory_order_relaxed);
    if (read == write_sequence_.load(std::memory_order_acquire)) return false;
    destination = slots_[read % history_mailbox_capacity].observation;
    read_sequence_.store(read + 1U, std::memory_order_release);
    return true;
}

std::uint64_t HistoryObservationMailbox::dropped_count() const noexcept
{
    return static_cast<std::uint64_t>(
        dropped_count_.load(std::memory_order_relaxed)
    );
}

bool HistoryObservationMailbox::changed(const app::SmokerSnapshotView& snapshot) const noexcept
{
    const auto fault = snapshot.active_fault
        ? std::optional<core::FaultCode>{snapshot.active_fault->code} : std::nullopt;
    return snapshot.session_status != previous_status_
        || snapshot.session_id != previous_session_id_
        || snapshot.chamber_target != previous_target_
        || snapshot.timer.started != previous_timer_.started
        || snapshot.timer.completed != previous_timer_.completed
        || fault != previous_fault_
        || !probes_equal(snapshot.probes, previous_probes_)
        || !alarms_equal(snapshot.active_alarms, previous_alarms_);
}

void HistoryObservationMailbox::remember(const app::SmokerSnapshotView& snapshot) noexcept
{
    previous_session_id_ = snapshot.session_id;
    previous_status_ = snapshot.session_status;
    previous_target_ = snapshot.chamber_target;
    previous_timer_ = snapshot.timer;
    previous_fault_ = snapshot.active_fault
        ? std::optional<core::FaultCode>{snapshot.active_fault->code} : std::nullopt;
    previous_probes_.clear();
    for (const auto& probe : snapshot.probes) {
        previous_probes_.push_back(HistoryProbe{
            probe.id, probe.role, probe.current_temperature, probe.target_temperature,
            probe.enabled, probe.alarm_enabled
        });
    }
    previous_alarms_.clear();
    for (const auto& alarm : snapshot.active_alarms) {
        previous_alarms_.push_back(HistoryAlarm{
            alarm.id, alarm.code, alarm.probe_id, alarm.acknowledged
        });
    }
}

bool HistoryObservationMailbox::push(
    const HistoryObservationKind kind,
    const app::SmokerSnapshotView& snapshot,
    const std::optional<std::int64_t> unix_utc_seconds
) noexcept
{
    const auto write = write_sequence_.load(std::memory_order_relaxed);
    const auto read = read_sequence_.load(std::memory_order_acquire);
    const auto pending = write - read;
    const bool lifecycle = kind == HistoryObservationKind::Start
        || kind == HistoryObservationKind::End;
    // Ordinary samples/changes cannot consume the last slot. In the normal
    // one-active-session lifecycle that leaves terminal END admission even
    // when the auxiliary consumer briefly falls behind.
    if (pending >= history_mailbox_capacity
        || (!lifecycle && pending >= history_mailbox_capacity - 1U)) {
        dropped_count_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    auto& observation = slots_[write % history_mailbox_capacity].observation;
    observation.kind = kind;
    observation.history_id = 0U;
    observation.sequence = 0U;
    observation.application_session_id = snapshot.session_id.value_or(0U);
    observation.session_status = snapshot.session_status;
    observation.stop_reason = snapshot.stop_reason;
    observation.session_elapsed = snapshot.session_elapsed;
    observation.unix_utc_seconds = unix_utc_seconds;
    observation.chamber_temperature = snapshot.chamber_temperature;
    observation.chamber_target = snapshot.chamber_target;
    observation.heater_demand = snapshot.heater_demand;
    observation.timer = snapshot.timer;
    observation.fault = snapshot.active_fault
        ? std::optional<core::FaultCode>{snapshot.active_fault->code} : std::nullopt;
    observation.probes.clear();
    for (const auto& probe : snapshot.probes) {
        observation.probes.push_back(HistoryProbe{
            probe.id, probe.role, probe.current_temperature, probe.target_temperature,
            probe.enabled, probe.alarm_enabled
        });
    }
    observation.alarms.clear();
    for (const auto& alarm : snapshot.active_alarms) {
        observation.alarms.push_back(HistoryAlarm{
            alarm.id, alarm.code, alarm.probe_id, alarm.acknowledged
        });
    }
    write_sequence_.store(write + 1U, std::memory_order_release);
    return true;
}

CircularHistoryLog::CircularHistoryLog(IHistoryFlash& flash)
    : flash_{flash}
{
}

bool CircularHistoryLog::initialize() noexcept
{
    if (flash_.sector_size() != history_page_bytes
        || flash_.size() < history_page_bytes
        || flash_.size() % history_page_bytes != 0U) {
        note_write_error();
        return false;
    }
    pages_.assign(flash_.size() / history_page_bytes, PageInfo{});
    for (std::size_t index = 0U; index < pages_.size(); ++index) {
        pages_[index].offset = index * history_page_bytes;
        // An unreadable sector is not unclaimed media. Leave initialization
        // incomplete so the HistoryTask retries the scan rather than allowing
        // a later append to erase a page whose retained content is unknown.
        if (!scan_page(index, pages_[index])) return false;
        if (pages_[index].valid) {
            next_generation_ = std::max(next_generation_, pages_[index].generation + 1U);
            next_history_id_ = std::max(next_history_id_, pages_[index].history_id + 1U);
        }
    }
    // A committed eviction marker makes the whole victim logically absent.
    // Finish any interrupted multi-sector erase before exposing queries or
    // accepting a new append; the marker sector is always erased last.
    while (true) {
        const auto pending = std::find_if(pages_.begin(), pages_.end(), [](const auto& page) {
            return page.valid && page.eviction_marker;
        });
        if (pending == pages_.end()) break;
        if (!complete_pending_eviction(pending->history_id, false)) return false;
    }
    return true;
}

std::optional<std::uint64_t> CircularHistoryLog::begin_session(
    HistoryObservation observation
) noexcept
{
    const auto history_id = next_history_id_++;
    active_history_id_ = history_id;
    next_sequence_ = 0U;
    if (!allocate_page(history_id, true)) {
        active_history_id_.reset();
        return std::nullopt;
    }
    observation.kind = HistoryObservationKind::Start;
    observation.history_id = history_id;
    if (!append_assigned(observation)) {
        active_history_id_.reset();
        return std::nullopt;
    }
    return history_id;
}

bool CircularHistoryLog::append(HistoryObservation observation) noexcept
{
    if (!active_history_id_) return false;
    observation.history_id = *active_history_id_;
    const bool ending = observation.kind == HistoryObservationKind::End;
    if (!append_assigned(observation)) return false;
    if (ending) {
        active_history_id_.reset();
        current_page_.reset();
    }
    return true;
}

void CircularHistoryLog::set_mailbox_drops(const std::uint64_t drops) noexcept
{
    mailbox_drops_ = drops;
}

HistoryHealth CircularHistoryLog::health() const noexcept
{
    std::size_t used = 0U;
    for (const auto& page : pages_) if (page.valid) used += page.used;
    HistoryStorageState state = HistoryStorageState::Ready;
    if (consecutive_write_errors_ >= failed_write_threshold) {
        state = HistoryStorageState::Failed;
    } else if (degraded_ || corrupt_records_ != 0U || write_errors_ != 0U
        || mailbox_drops_ != 0U) {
        state = HistoryStorageState::Degraded;
    }
    return HistoryHealth{
        state, mailbox_drops_, corrupt_records_, write_errors_, flash_.size(), used
    };
}

std::vector<HistorySessionSummary> CircularHistoryLog::sessions(
    const std::optional<std::uint64_t> before, const std::size_t limit
)
{
    last_query_failed_ = false;
    if (limit == 0U) return {};
    std::vector<PageInfo> ordered;
    for (const auto& page : pages_) {
        if (page.valid && !session_is_pending_eviction(page.history_id)) {
            ordered.push_back(page);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const PageInfo& left, const PageInfo& right) {
        return left.generation < right.generation;
    });
    std::map<std::uint64_t, HistorySessionSummary> summaries;
    std::map<std::uint64_t, std::uint32_t> first_sequences;
    for (const auto& page : ordered) {
        std::vector<HistoryObservation> records;
        if (!read_page_records(page, records)) return {};
        for (const auto& record : records) {
            auto& summary = summaries[record.history_id];
            if (summary.history_id == 0U) {
                summary.history_id = record.history_id;
                summary.application_session_id = record.application_session_id;
                summary.final_status = record.session_status;
                first_sequences[record.history_id] = record.sequence;
            }
            if (record.kind == HistoryObservationKind::Start) {
                summary.start_unix_utc_seconds = record.unix_utc_seconds;
            }
            if (record.kind == HistoryObservationKind::End) {
                summary.end_unix_utc_seconds = record.unix_utc_seconds;
                summary.stop_reason = record.stop_reason;
            }
            if (record.kind == HistoryObservationKind::Sample) ++summary.sample_count;
            summary.elapsed = std::max(summary.elapsed, record.session_elapsed);
            summary.final_status = record.session_status;
        }
    }
    std::vector<HistorySessionSummary> result;
    for (auto iterator = summaries.rbegin(); iterator != summaries.rend(); ++iterator) {
        auto summary = iterator->second;
        if (before && summary.history_id >= *before) continue;
        summary.active = active_history_id_ == summary.history_id;
        summary.interrupted = !summary.active
            && summary.stop_reason == core::StopReason::None;
        summary.truncated = first_sequences[summary.history_id] != 0U;
        result.push_back(summary);
        if (result.size() == limit) break;
    }
    return result;
}

bool CircularHistoryLog::contains_session(const std::uint64_t history_id) const noexcept
{
    return std::any_of(pages_.begin(), pages_.end(), [history_id](const auto& page) {
        return page.valid && page.history_id == history_id;
    }) && !session_is_pending_eviction(history_id);
}

std::vector<HistoryObservation> CircularHistoryLog::samples(
    const std::uint64_t history_id,
    const std::optional<std::uint32_t> after,
    const std::size_t limit,
    const std::uint16_t stride,
    std::optional<std::uint32_t>& continuation
)
{
    last_query_failed_ = false;
    continuation.reset();
    if (limit == 0U) return {};
    std::vector<PageInfo> ordered;
    for (const auto& page : pages_) {
        if (page.valid && page.history_id == history_id
            && !session_is_pending_eviction(history_id)) ordered.push_back(page);
    }
    std::sort(ordered.begin(), ordered.end(), [](const PageInfo& left, const PageInfo& right) {
        return left.generation < right.generation;
    });
    std::vector<HistoryObservation> result;
    result.reserve(limit);
    std::uint32_t periodic_index = 0U;
    const auto effective_stride = std::max<std::uint16_t>(stride, 1U);
    for (const auto& page : ordered) {
        std::vector<HistoryObservation> page_records;
        if (!read_page_records(page, page_records)) {
            continuation.reset();
            return {};
        }
        for (auto& observation : page_records) {
            const bool include = observation.kind != HistoryObservationKind::Sample
                || periodic_index++ % effective_stride == 0U;
            if (after && observation.sequence <= *after) continue;
            if (!include) continue;
            if (result.size() == limit) {
                continuation = result.back().sequence;
                return result;
            }
            result.push_back(std::move(observation));
        }
    }
    return result;
}

bool CircularHistoryLog::last_query_failed() const noexcept
{
    return last_query_failed_;
}

bool CircularHistoryLog::scan_page(
    const std::size_t page_index, PageInfo& page
) noexcept
{
    std::array<std::uint8_t, history_page_bytes> bytes{};
    if (!flash_.read(page_index * history_page_bytes, bytes)) {
        note_write_error();
        return false;
    }
    const bool erased = std::all_of(bytes.begin(), bytes.end(), [](const auto byte) {
        return byte == 0xFFU;
    });
    if (erased || get<std::uint32_t>(bytes, 0U) != page_magic) return true;
    if (get<std::uint16_t>(bytes, 4U) != format_version
        || get<std::uint16_t>(bytes, 6U) != page_header_bytes
        || get<std::uint32_t>(bytes, 32U) != commit_marker
        || get<std::uint32_t>(bytes, 28U) != crc32(
            std::span<const std::uint8_t>{bytes.data(), 28U}
        )) {
        degraded_ = true;
        ++corrupt_records_;
        return true;
    }
    page.offset = page_index * history_page_bytes;
    page.generation = get<std::uint64_t>(bytes, 8U);
    page.history_id = get<std::uint64_t>(bytes, 16U);
    page.page_index = get<std::uint32_t>(bytes, 24U);
    page.used = page_header_bytes;
    page.valid = true;
    const auto stored_eviction_marker = get<std::uint32_t>(bytes, 36U);
    page.eviction_marker = is_eviction_marker_state(stored_eviction_marker);
    page.eviction_marker_writable =
        (stored_eviction_marker & eviction_marker) == eviction_marker;
    if (page.eviction_marker && stored_eviction_marker != eviction_marker) {
        degraded_ = true;
        ++corrupt_records_;
    }
    std::size_t cursor = page_header_bytes;
    while (cursor + record_header_bytes <= bytes.size()) {
        if (std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end(),
                [](const auto byte) { return byte == 0xFFU; })) break;
        const auto header = std::span<const std::uint8_t>{
            bytes.data() + cursor, record_header_bytes
        };
        if (get<std::uint32_t>(header, 0U) != record_magic
            || get<std::uint16_t>(header, 4U) != format_version
            || get<std::uint8_t>(header, 6U)
                > static_cast<std::uint8_t>(HistoryObservationKind::End)
            || get<std::uint16_t>(header, 10U) != record_header_bytes
            || get<std::uint32_t>(header, 36U) != commit_marker
            || get<std::uint32_t>(header, 28U) != crc32(header.first(28U))) {
            degraded_ = true;
            ++corrupt_records_;
            break;
        }
        const auto payload_size = get<std::uint16_t>(header, 8U);
        if (cursor + record_header_bytes + payload_size > bytes.size()) {
            degraded_ = true;
            ++corrupt_records_;
            break;
        }
        const auto payload = std::span<const std::uint8_t>{
            bytes.data() + cursor + record_header_bytes, payload_size
        };
        HistoryObservation decoded;
        if (crc32(payload) != get<std::uint32_t>(header, 24U)
            || !decode_observation(payload, decoded)) {
            degraded_ = true;
            ++corrupt_records_;
            break;
        }
        cursor += record_header_bytes + payload_size;
        page.used = cursor;
    }
    return true;
}

bool CircularHistoryLog::allocate_page(
    const std::uint64_t history_id, const bool session_start
) noexcept
{
    const auto selected = select_page_for_reuse(history_id);
    if (!selected) {
        note_write_error();
        return false;
    }
    auto& page = pages_[*selected];
    if (!flash_.erase_sector(page.offset)) {
        note_write_error();
        return false;
    }
    std::uint32_t page_index = 0U;
    if (!session_start) {
        for (const auto& existing : pages_) {
            if (existing.valid && existing.history_id == history_id) {
                page_index = std::max(page_index, existing.page_index + 1U);
            }
        }
    }
    page = PageInfo{
        *selected * history_page_bytes, next_generation_++, history_id,
        page_index, page_header_bytes, true, false, true
    };
    if (!write_page_header(page)) {
        page.valid = false;
        note_write_error();
        return false;
    }
    current_page_ = *selected;
    return true;
}

bool CircularHistoryLog::write_page_header(PageInfo& page) noexcept
{
    std::array<std::uint8_t, page_header_bytes> header{};
    header.fill(0xFFU);
    put(header, 0U, page_magic);
    put(header, 4U, format_version);
    put(header, 6U, static_cast<std::uint16_t>(page_header_bytes));
    put(header, 8U, page.generation);
    put(header, 16U, page.history_id);
    put(header, 24U, page.page_index);
    put(header, 28U, crc32(std::span<const std::uint8_t>{header.data(), 28U}));
    if (!flash_.write(page.offset, std::span<const std::uint8_t>{header.data(), 32U})) {
        return false;
    }
    std::array<std::uint8_t, 4U> commit{};
    put(commit, 0U, commit_marker);
    return flash_.write(page.offset + 32U, commit);
}

bool CircularHistoryLog::append_assigned(HistoryObservation& observation) noexcept
{
    observation.sequence = next_sequence_++;
    const auto payload = encode_observation(observation);
    if (payload.size() > std::numeric_limits<std::uint16_t>::max()
        || record_header_bytes + payload.size() > history_page_bytes - page_header_bytes) {
        note_write_error();
        return false;
    }
    if (!current_page_ || !pages_[*current_page_].valid
        || pages_[*current_page_].history_id != observation.history_id
        || pages_[*current_page_].used + record_header_bytes + payload.size()
            > history_page_bytes) {
        if (!allocate_page(observation.history_id, false)) return false;
    }
    auto& page = pages_[*current_page_];
    std::array<std::uint8_t, record_header_bytes> header{};
    header.fill(0xFFU);
    put(header, 0U, record_magic);
    put(header, 4U, format_version);
    put(header, 6U, static_cast<std::uint8_t>(observation.kind));
    put(header, 8U, static_cast<std::uint16_t>(payload.size()));
    put(header, 10U, static_cast<std::uint16_t>(record_header_bytes));
    put(header, 12U, observation.sequence);
    put(header, 16U, observation.history_id);
    put(header, 24U, crc32(payload));
    put(header, 28U, crc32(std::span<const std::uint8_t>{header.data(), 28U}));
    const auto offset = page.offset + page.used;
    if (!flash_.write(offset, std::span<const std::uint8_t>{header.data(), 36U})
        || !flash_.write(offset + record_header_bytes, payload)) {
        // A failed program may have changed an arbitrary prefix. Never try to
        // overwrite that tail without an erase; continue on a fresh page.
        current_page_.reset();
        note_write_error();
        return false;
    }
    std::array<std::uint8_t, 4U> commit{};
    put(commit, 0U, commit_marker);
    if (!flash_.write(offset + 36U, commit)) {
        current_page_.reset();
        note_write_error();
        return false;
    }
    page.used += record_header_bytes + payload.size();
    consecutive_write_errors_ = 0U;
    return true;
}

bool CircularHistoryLog::read_page_records(
    const PageInfo& page, std::vector<HistoryObservation>& destination
)
{
    std::array<std::uint8_t, history_page_bytes> bytes{};
    if (!flash_.read(page.offset, bytes)) {
        note_corrupt_record();
        return false;
    }
    std::size_t cursor = page_header_bytes;
    while (cursor + record_header_bytes <= page.used) {
        const auto header = std::span<const std::uint8_t>{
            bytes.data() + cursor, record_header_bytes
        };
        if (get<std::uint32_t>(header, 0U) != record_magic
            || get<std::uint16_t>(header, 4U) != format_version
            || get<std::uint8_t>(header, 6U)
                > static_cast<std::uint8_t>(HistoryObservationKind::End)
            || get<std::uint16_t>(header, 10U) != record_header_bytes
            || get<std::uint32_t>(header, 36U) != commit_marker
            || get<std::uint32_t>(header, 28U) != crc32(header.first(28U))) {
            note_corrupt_record();
            return false;
        }
        const auto payload_size = get<std::uint16_t>(header, 8U);
        if (cursor + record_header_bytes + payload_size > page.used) {
            note_corrupt_record();
            return false;
        }
        HistoryObservation observation;
        observation.kind = static_cast<HistoryObservationKind>(get<std::uint8_t>(header, 6U));
        observation.sequence = get<std::uint32_t>(header, 12U);
        observation.history_id = get<std::uint64_t>(header, 16U);
        const auto payload = std::span<const std::uint8_t>{
            bytes.data() + cursor + record_header_bytes, payload_size
        };
        if (crc32(payload) != get<std::uint32_t>(header, 24U)
            || !decode_observation(payload, observation)) {
            note_corrupt_record();
            return false;
        }
        destination.push_back(std::move(observation));
        cursor += record_header_bytes + payload_size;
    }
    return true;
}

std::optional<std::size_t> CircularHistoryLog::select_page_for_reuse(
    const std::uint64_t history_id
) noexcept
{
    const auto pending = std::find_if(pages_.begin(), pages_.end(), [](const auto& page) {
        return page.valid && page.eviction_marker;
    });
    if (pending != pages_.end()) {
        return complete_pending_eviction(pending->history_id, true);
    }

    for (std::size_t index = 0U; index < pages_.size(); ++index) {
        if (!pages_[index].valid) return index;
    }

    std::map<std::uint64_t, bool> completed;
    std::map<std::uint64_t, std::uint64_t> oldest_generation;
    for (const auto& page : pages_) {
        oldest_generation[page.history_id] = oldest_generation.contains(page.history_id)
            ? std::min(oldest_generation[page.history_id], page.generation)
            : page.generation;
        std::vector<HistoryObservation> records;
        static_cast<void>(read_page_records(page, records));
        for (const auto& record : records) {
            if (record.kind == HistoryObservationKind::End) completed[page.history_id] = true;
        }
    }
    auto choose_oldest = [&](const bool only_completed, const bool exclude_current)
        -> std::optional<std::uint64_t> {
        std::optional<std::uint64_t> chosen;
        std::uint64_t generation = std::numeric_limits<std::uint64_t>::max();
        for (const auto& [candidate, oldest] : oldest_generation) {
            if ((only_completed && !completed[candidate])
                || (exclude_current && candidate == history_id)) continue;
            if (oldest < generation) {
                generation = oldest;
                chosen = candidate;
            }
        }
        return chosen;
    };
    auto victim = choose_oldest(true, true);
    if (!victim) victim = choose_oldest(false, true);
    if (victim) {
        std::optional<std::size_t> marker_page;
        for (std::size_t index = 0U; index < pages_.size(); ++index) {
            if (pages_[index].valid && pages_[index].history_id == *victim
                && pages_[index].eviction_marker_writable) {
                marker_page = index;
                break;
            }
        }
        if (!marker_page || !mark_session_for_eviction(*marker_page)) {
            return std::nullopt;
        }
        return complete_pending_eviction(*victim, true);
    }

    // A single session owns the entire partition. Reuse its oldest page; the
    // missing sequence zero makes `truncated` reconstructible after reboot.
    const auto oldest = std::min_element(
        pages_.begin(), pages_.end(), [](const PageInfo& left, const PageInfo& right) {
            return left.generation < right.generation;
        }
    );
    if (oldest == pages_.end()) return std::nullopt;
    const auto index = static_cast<std::size_t>(oldest - pages_.begin());
    pages_[index].valid = false;
    return index;
}

bool CircularHistoryLog::mark_session_for_eviction(const std::size_t page_index) noexcept
{
    auto& page = pages_[page_index];
    std::array<std::uint8_t, sizeof(eviction_marker)> marker{};
    put(marker, 0U, eviction_marker);
    const bool write_succeeded = flash_.write(page.offset + 36U, marker);

    // A flash adapter may report failure after programming a prefix or even
    // the complete marker. Read back the commit word before deciding whether
    // the transaction is visible and safe to continue.
    std::array<std::uint8_t, sizeof(eviction_marker)> stored{};
    if (!flash_.read(page.offset + 36U, stored)) {
        note_write_error();
        return false;
    }
    const auto stored_marker = get<std::uint32_t>(stored, 0U);
    page.eviction_marker = is_eviction_marker_state(stored_marker);
    page.eviction_marker_writable =
        (stored_marker & eviction_marker) == eviction_marker;
    if (!write_succeeded) note_write_error();
    if (!page.eviction_marker) {
        if (write_succeeded) note_write_error();
        return false;
    }
    return true;
}

std::optional<std::size_t> CircularHistoryLog::complete_pending_eviction(
    const std::uint64_t history_id, const bool keep_marker_for_reuse
) noexcept
{
    const auto marker = std::find_if(
        pages_.begin(), pages_.end(), [history_id](const auto& page) {
            return page.valid && page.history_id == history_id && page.eviction_marker;
        }
    );
    if (marker == pages_.end()) return std::nullopt;
    const auto marker_index = static_cast<std::size_t>(marker - pages_.begin());

    for (std::size_t index = 0U; index < pages_.size(); ++index) {
        if (index == marker_index || !pages_[index].valid
            || pages_[index].history_id != history_id) continue;
        if (!flash_.erase_sector(pages_[index].offset)) {
            note_write_error();
            return std::nullopt;
        }
        pages_[index].valid = false;
        pages_[index].eviction_marker = false;
        pages_[index].eviction_marker_writable = true;
    }

    // The marker is the transaction commit record and must survive until all
    // other pages of the victim are gone. The allocation path erases it as the
    // final operation itself, avoiding a redundant erase of the reused sector.
    if (keep_marker_for_reuse) return marker_index;

    // Startup has no immediate allocation to perform, so finish recovery by
    // erasing the marker only after every other victim page is gone.
    if (!flash_.erase_sector(pages_[marker_index].offset)) {
        note_write_error();
        return std::nullopt;
    }
    pages_[marker_index].valid = false;
    pages_[marker_index].eviction_marker = false;
    pages_[marker_index].eviction_marker_writable = true;
    return marker_index;
}

bool CircularHistoryLog::session_is_pending_eviction(
    const std::uint64_t history_id
) const noexcept
{
    return std::any_of(pages_.begin(), pages_.end(), [history_id](const auto& page) {
        return page.valid && page.history_id == history_id && page.eviction_marker;
    });
}

void CircularHistoryLog::note_write_error() noexcept
{
    ++write_errors_;
    if (consecutive_write_errors_ < std::numeric_limits<std::uint32_t>::max()) {
        ++consecutive_write_errors_;
    }
    degraded_ = true;
}

void CircularHistoryLog::note_corrupt_record() noexcept
{
    if (corrupt_records_ < std::numeric_limits<std::uint64_t>::max()) {
        ++corrupt_records_;
    }
    degraded_ = true;
    last_query_failed_ = true;
}

} // namespace smoker::platform

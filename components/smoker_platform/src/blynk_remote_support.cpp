#include "smoker/platform/blynk_remote_support.hpp"

#include "smoker/core/domain.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace smoker::platform {
namespace {

template <std::size_t Size>
void copy_text(std::array<char, Size>& destination, const std::string_view value) noexcept
{
    destination.fill('\0');
    const auto count = std::min(value.size(), Size - 1U);
    std::memcpy(destination.data(), value.data(), count);
}

template <std::size_t Size>
void append_text(
    std::array<char, Size>& destination,
    std::size_t& length,
    const std::string_view value
) noexcept
{
    if (length >= Size - 1U) return;
    const auto count = std::min(value.size(), (Size - 1U) - length);
    std::memcpy(destination.data() + length, value.data(), count);
    length += count;
    destination[length] = '\0';
}

template <std::size_t Size>
void append_probe_name(
    std::array<char, Size>& destination,
    std::size_t& length,
    const std::string_view value
) noexcept
{
    for (const char byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        const bool safe = character >= 0x20U && character <= 0x7EU
            && byte != ':' && byte != ';' && byte != '"' && byte != '\\';
        const char rendered = safe ? byte : '?';
        append_text(destination, length, {&rendered, 1U});
    }
}

template <typename Integer, std::size_t Size>
void append_integer(
    std::array<char, Size>& destination,
    std::size_t& length,
    const Integer value
) noexcept
{
    std::array<char, 24U> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec == std::errc{}) {
        append_text(destination, length, {
            buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())
        });
    }
}

template <std::size_t Size>
void append_deci(
    std::array<char, Size>& destination,
    std::size_t& length,
    const std::optional<core::Temperature>& temperature
) noexcept
{
    if (!temperature) {
        append_text(destination, length, "-");
        return;
    }
    const auto deci = static_cast<std::int32_t>(
        std::lround(temperature->celsius() * 10.0F)
    );
    const auto magnitude = deci < 0
        ? -static_cast<std::int64_t>(deci) : static_cast<std::int64_t>(deci);
    if (deci < 0) append_text(destination, length, "-");
    append_integer(destination, length, magnitude / 10);
    append_text(destination, length, ".");
    append_integer(destination, length, magnitude % 10);
}

[[nodiscard]] std::string_view session_status_name(const core::SessionStatus status) noexcept
{
    switch (status) {
    case core::SessionStatus::Idle: return "IDLE";
    case core::SessionStatus::Running: return "RUNNING";
    case core::SessionStatus::Stopped: return "STOPPED";
    case core::SessionStatus::Fault: return "FAULT";
    }
    return "FAULT";
}

[[nodiscard]] std::string_view timer_state_name(
    const bool configured,
    const core::TimerRuntimeState& timer
) noexcept
{
    if (!configured) return "NONE";
    if (!timer.started) return timer.completed ? "COMPLETED" : "WAITING";
    return timer.completed ? "COMPLETED" : "RUNNING";
}

[[nodiscard]] std::string_view fault_name(const std::optional<core::Fault>& fault) noexcept
{
    if (!fault) return {};
    switch (fault->code) {
    case core::FaultCode::ChamberSensorInvalid: return "CHAMBER_SENSOR_INVALID";
    case core::FaultCode::ChamberOverTemperature: return "CHAMBER_OVER_TEMPERATURE";
    case core::FaultCode::ControlLoopFailure: return "CONTROL_LOOP_FAILURE";
    case core::FaultCode::ConfigurationInvalid: return "CONFIGURATION_INVALID";
    }
    return "CONFIGURATION_INVALID";
}

[[nodiscard]] std::int32_t deci_celsius(
    const std::optional<core::Temperature>& temperature
) noexcept
{
    return temperature
        ? static_cast<std::int32_t>(std::lround(temperature->celsius() * 10.0F))
        : -2731;
}

class JsonWriter final {
public:
    explicit JsonWriter(BlynkPayload& payload) noexcept : payload_{payload} {}

    bool begin() noexcept { return raw("{"); }
    bool end() noexcept { return raw("}"); }

    bool string_field(const std::string_view name, const std::string_view value) noexcept
    {
        return field_prefix(name) && raw("\"") && escaped(value) && raw("\"");
    }

    bool integer_field(const std::string_view name, const std::int32_t value) noexcept
    {
        if (!field_prefix(name)) return false;
        std::array<char, 16U> buffer{};
        const auto result = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), value
        );
        return result.ec == std::errc{}
            && raw({buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
    }

    bool deci_field(const std::string_view name, const std::int32_t value) noexcept
    {
        if (!field_prefix(name)) return false;
        std::array<char, 24U> buffer{};
        std::size_t length = 0U;
        const auto magnitude = value < 0
            ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
        if (value < 0) buffer[length++] = '-';
        const auto whole_result = std::to_chars(
            buffer.data() + length, buffer.data() + buffer.size(), magnitude / 10
        );
        if (whole_result.ec != std::errc{}) return false;
        length = static_cast<std::size_t>(whole_result.ptr - buffer.data());
        buffer[length++] = '.';
        buffer[length++] = static_cast<char>('0' + (magnitude % 10));
        return raw({buffer.data(), length});
    }

private:
    bool field_prefix(const std::string_view name) noexcept
    {
        if (!first_ && !raw(",")) return false;
        first_ = false;
        return raw("\"") && raw(name) && raw("\":");
    }

    bool escaped(const std::string_view value) noexcept
    {
        for (const char byte : value) {
            const auto character = static_cast<unsigned char>(byte);
            if (character == '"' || character == '\\') {
                const std::array<char, 2U> escaped_value{
                    '\\', static_cast<char>(character)
                };
                if (!raw({escaped_value.data(), escaped_value.size()})) return false;
            } else if (character >= 0x20U && character <= 0x7EU) {
                const char ascii = static_cast<char>(character);
                if (!raw({&ascii, 1U})) return false;
            } else if (!raw("?")) {
                return false;
            }
        }
        return true;
    }

    bool raw(const std::string_view value) noexcept
    {
        if (payload_.length + value.size() > blynk_payload_capacity) return false;
        std::memcpy(payload_.bytes.data() + payload_.length, value.data(), value.size());
        payload_.length += value.size();
        payload_.bytes[payload_.length] = '\0';
        return true;
    }

    BlynkPayload& payload_;
    bool first_{true};
};

[[nodiscard]] std::string_view feedback_kind_name(
    const BlynkCommandResultKind kind
) noexcept
{
    switch (kind) {
    case BlynkCommandResultKind::SemanticAccepted: return "accepted";
    case BlynkCommandResultKind::SemanticRejected: return "rejected";
    case BlynkCommandResultKind::ServiceAccepted: return "service_accepted";
    case BlynkCommandResultKind::ServiceRejected: return "service_rejected";
    }
    return "rejected";
}

} // namespace

BlynkRemoteStatus make_blynk_remote_status(
    const app::SmokerSnapshotView& snapshot,
    const FirmwareUpdateStatus& firmware
) noexcept
{
    BlynkRemoteStatus result{};
    copy_text(result.session_status, session_status_name(snapshot.session_status));
    const auto session_seconds = std::max<std::int64_t>(
        0, snapshot.session_elapsed.count() / 1000
    );
    result.session_elapsed_seconds = static_cast<std::int32_t>(std::min<std::int64_t>(
        session_seconds, std::numeric_limits<std::int32_t>::max()
    ));
    result.chamber_current_deci_celsius = deci_celsius(snapshot.chamber_temperature);
    result.chamber_target_deci_celsius = deci_celsius(snapshot.chamber_target);
    result.heater_percent = static_cast<std::int32_t>(
        std::clamp(std::lround(snapshot.heater_demand.percent()), 0L, 100L)
    );
    copy_text(result.timer_state, timer_state_name(snapshot.timer_configured, snapshot.timer));
    if (snapshot.timer_configured) {
        const auto timer_seconds = std::max<std::int64_t>(
            0, snapshot.timer.elapsed.count() / 1000
        );
        result.timer_elapsed_seconds = static_cast<std::int32_t>(
            std::min<std::int64_t>(timer_seconds, std::numeric_limits<std::int32_t>::max())
        );
    }

    std::size_t probe_length = 0U;
    constexpr std::size_t blynk_probe_capacity = 6U;
    for (std::size_t index = 0U;
         index < snapshot.probes.size() && index < blynk_probe_capacity;
         ++index) {
        const auto& probe = snapshot.probes[index];
        if (probe_length != 0U) append_text(result.probe_summary, probe_length, ";");
        append_integer(result.probe_summary, probe_length, probe.id);
        append_text(result.probe_summary, probe_length, ":");
        append_probe_name(result.probe_summary, probe_length, probe.name);
        append_text(result.probe_summary, probe_length, ":");
        append_deci(result.probe_summary, probe_length, probe.current_temperature);
        append_text(result.probe_summary, probe_length, ":");
        append_deci(result.probe_summary, probe_length, probe.target_temperature);
        append_text(result.probe_summary, probe_length, probe.enabled ? ":1:" : ":0:");
        append_text(result.probe_summary, probe_length, probe.alarm_enabled ? "1" : "0");
    }
    result.active_alarm_count = static_cast<std::int32_t>(std::min<std::size_t>(
        snapshot.active_alarms.size(),
        static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
    ));
    copy_text(result.fault_code, fault_name(snapshot.active_fault));
    copy_text(result.firmware_state, firmware_update_state_name(firmware.state));
    copy_text(result.firmware_current_version, firmware.current_version.data());
    if (firmware.state == FirmwareUpdateState::Available
        && firmware.available_version[0] != '\0') {
        copy_text(result.firmware_available_version, firmware.available_version.data());
    } else if (firmware.state == FirmwareUpdateState::UpToDate) {
        copy_text(result.firmware_available_version, "Latest");
    }
    result.firmware_progress_percent = static_cast<std::int32_t>(std::min<std::uint8_t>(
        firmware.progress_percent, 100U
    ));
    copy_text(result.firmware_error, firmware.error.data());
    return result;
}

std::optional<BlynkPayload> serialize_blynk_batch(
    const BlynkRemoteStatus& status
) noexcept
{
    BlynkPayload result{};
    JsonWriter writer{result};
    const bool success = writer.begin()
        && writer.string_field("SessionStatus", status.session_status.data())
        && writer.integer_field("SessionElapsedSeconds", status.session_elapsed_seconds)
        && writer.deci_field("ChamberCurrentC", status.chamber_current_deci_celsius)
        && writer.deci_field("ChamberTargetC", status.chamber_target_deci_celsius)
        && writer.integer_field("HeaterPercent", status.heater_percent)
        && writer.string_field("TimerState", status.timer_state.data())
        && writer.integer_field("TimerElapsedSeconds", status.timer_elapsed_seconds)
        && writer.string_field("ProbeSummary", status.probe_summary.data())
        && writer.integer_field("ActiveAlarmCount", status.active_alarm_count)
        && writer.string_field("FaultCode", status.fault_code.data())
        && writer.string_field("FirmwareState", status.firmware_state.data())
        && writer.string_field("FirmwareCurrentVersion", status.firmware_current_version.data())
        && writer.string_field("FirmwareAvailableVersion", status.firmware_available_version.data())
        && writer.integer_field("FirmwareProgressPercent", status.firmware_progress_percent)
        && writer.string_field("FirmwareError", status.firmware_error.data())
        && writer.end();
    return success ? std::optional<BlynkPayload>{result} : std::nullopt;
}

void BlynkRemoteProjection::observe(const BlynkRemoteStatus status) noexcept
{
    pending_ = status;
    has_pending_ = true;
    dirty_ = !has_last_publish_ || pending_ != last_published_;
}

void BlynkRemoteProjection::connected() noexcept
{
    connected_ = true;
    publish_on_connect_ = true;
    dirty_ = has_pending_;
}

void BlynkRemoteProjection::disconnected() noexcept
{
    connected_ = false;
    publish_on_connect_ = false;
}

std::optional<BlynkRemoteStatus> BlynkRemoteProjection::pending_publish(
    const std::int64_t now_monotonic_ms
) const noexcept
{
    if (!connected_ || !has_pending_ || !dirty_) return std::nullopt;
    if (!publish_on_connect_ && has_last_publish_
        && now_monotonic_ms - last_publish_ms_ < blynk_status_minimum_interval_ms) {
        return std::nullopt;
    }
    return pending_;
}

void BlynkRemoteProjection::publish_succeeded(
    const std::int64_t now_monotonic_ms
) noexcept
{
    if (!connected_ || !has_pending_) return;
    last_published_ = pending_;
    dirty_ = false;
    publish_on_connect_ = false;
    has_last_publish_ = true;
    last_publish_ms_ = now_monotonic_ms;
}

bool BlynkRemoteProjection::dirty() const noexcept { return dirty_; }

bool BlynkCommandResults::track(const std::uint32_t correlation_id) noexcept
{
    if (correlation_id == 0U || pending_count_ == capacity) return false;
    if (std::find(pending_.begin(), pending_.begin() + pending_count_, correlation_id)
        != pending_.begin() + pending_count_) {
        return true;
    }
    pending_[pending_count_++] = correlation_id;
    return true;
}

void BlynkCommandResults::observe(
    const std::span<const app::CommandResultView> results
) noexcept
{
    for (const auto& result : results) {
        const auto end = pending_.begin() + pending_count_;
        const auto found = std::find(pending_.begin(), end, result.correlation_id);
        if (found == end) continue;
        if (!enqueue(BlynkCommandFeedback{
            result.correlation_id,
            result.semantic_accepted
                ? BlynkCommandResultKind::SemanticAccepted
                : BlynkCommandResultKind::SemanticRejected,
        })) {
            // Keep the correlation pending until the fixed feedback queue has
            // room. This avoids turning a transient MQTT backlog into a lost
            // semantic result.
            continue;
        }
        std::move(found + 1, end, found);
        --pending_count_;
    }
}

bool BlynkCommandResults::record_service_result(
    const std::uint32_t correlation_id,
    const bool accepted
) noexcept
{
    return enqueue(BlynkCommandFeedback{
        correlation_id,
        accepted ? BlynkCommandResultKind::ServiceAccepted
                 : BlynkCommandResultKind::ServiceRejected,
    });
}

bool BlynkCommandResults::enqueue(const BlynkCommandFeedback feedback) noexcept
{
    if (feedback_count_ == capacity || feedback.correlation_id == 0U) return false;
    feedback_[(feedback_head_ + feedback_count_) % capacity] = feedback;
    ++feedback_count_;
    return true;
}

std::optional<BlynkCommandFeedback> BlynkCommandResults::pop() noexcept
{
    if (feedback_count_ == 0U) return std::nullopt;
    const auto result = feedback_[feedback_head_];
    feedback_head_ = (feedback_head_ + 1U) % capacity;
    --feedback_count_;
    return result;
}

void BlynkCommandResults::disconnected() noexcept
{
    pending_count_ = 0U;
    feedback_head_ = 0U;
    feedback_count_ = 0U;
}

std::size_t BlynkCommandResults::pending_count() const noexcept
{
    return pending_count_;
}

BlynkPayload serialize_blynk_command_feedback(
    const BlynkCommandFeedback feedback
) noexcept
{
    BlynkPayload result{};
    std::size_t length = 0U;
    append_integer(result.bytes, length, feedback.correlation_id);
    append_text(result.bytes, length, ":");
    append_text(result.bytes, length, feedback_kind_name(feedback.kind));
    result.length = length;
    return result;
}

std::string_view blynk_event_code(const BlynkEventType type) noexcept
{
    switch (type) {
    case BlynkEventType::Fault: return "smoker_fault";
    case BlynkEventType::Alarm: return "smoker_alarm";
    case BlynkEventType::SessionDone: return "smoker_session_done";
    case BlynkEventType::Ota: return "smoker_ota";
    case BlynkEventType::RemoteError: return "smoker_remote_error";
    case BlynkEventType::Count: break;
    }
    return "smoker_remote_error";
}

void BlynkEventScheduler::queue(
    const BlynkEventType type,
    const std::string_view description
) noexcept
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= type_count) return;
    messages_[index].type = type;
    copy_text(messages_[index].description, description);
    pending_[index] = true;
}

std::optional<BlynkEventMessage> BlynkEventScheduler::pending_publish(
    const std::int64_t now_monotonic_ms
) const noexcept
{
    for (std::size_t index = 0U; index < type_count; ++index) {
        if (pending_[index]
            && (!has_last_publish_[index]
                || now_monotonic_ms - last_publish_ms_[index]
                    >= blynk_status_minimum_interval_ms)) {
            return messages_[index];
        }
    }
    return std::nullopt;
}

void BlynkEventScheduler::publish_succeeded(
    const BlynkEventType type,
    const std::int64_t now_monotonic_ms
) noexcept
{
    const auto index = static_cast<std::size_t>(type);
    if (index >= type_count) return;
    pending_[index] = false;
    has_last_publish_[index] = true;
    last_publish_ms_[index] = now_monotonic_ms;
}

void BlynkEventScheduler::disconnected() noexcept
{
    pending_.fill(false);
}

} // namespace smoker::platform

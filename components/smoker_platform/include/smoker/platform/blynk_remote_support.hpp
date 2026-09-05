#pragma once

#include "smoker/app/snapshot_view.hpp"
#include "smoker/platform/firmware_update_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace smoker::platform {

inline constexpr std::int64_t blynk_status_minimum_interval_ms = 5000;
inline constexpr std::size_t blynk_public_message_limit = 1024U;
// Leaves room for the MQTT fixed/variable header and the `batch_ds` topic.
inline constexpr std::size_t blynk_payload_capacity = 960U;

struct BlynkRemoteStatus final {
    std::array<char, 9U> session_status{};
    std::int32_t session_elapsed_seconds{0};
    std::int32_t chamber_current_deci_celsius{-2731};
    std::int32_t chamber_target_deci_celsius{-2731};
    std::int32_t heater_percent{0};
    std::array<char, 10U> timer_state{};
    std::int32_t timer_elapsed_seconds{-1};
    std::array<char, 193U> probe_summary{};
    std::int32_t active_alarm_count{0};
    std::array<char, 32U> fault_code{};
    std::array<char, 18U> firmware_state{};
    std::array<char, 32U> firmware_current_version{};
    std::array<char, 32U> firmware_available_version{};
    std::int32_t firmware_progress_percent{0};
    std::array<char, 128U> firmware_error{};

    friend bool operator==(const BlynkRemoteStatus&, const BlynkRemoteStatus&) = default;
};

struct BlynkPayload final {
    std::array<char, blynk_payload_capacity + 1U> bytes{};
    std::size_t length{0U};

    [[nodiscard]] std::string_view view() const noexcept
    {
        return {bytes.data(), length};
    }
};

[[nodiscard]] BlynkRemoteStatus make_blynk_remote_status(
    const app::SmokerSnapshotView& snapshot,
    const FirmwareUpdateStatus& firmware
) noexcept;

[[nodiscard]] std::optional<BlynkPayload> serialize_blynk_batch(
    const BlynkRemoteStatus& status
) noexcept;

class BlynkRemoteProjection final {
public:
    void observe(BlynkRemoteStatus status) noexcept;
    void connected() noexcept;
    void disconnected() noexcept;

    // Returns the newest candidate without committing it. Network failure can
    // therefore leave it dirty. Call publish_succeeded() only after ESP-MQTT
    // accepts the QoS-0 message for transmission.
    [[nodiscard]] std::optional<BlynkRemoteStatus> pending_publish(
        std::int64_t now_monotonic_ms
    ) const noexcept;
    void publish_succeeded(std::int64_t now_monotonic_ms) noexcept;
    [[nodiscard]] bool dirty() const noexcept;

private:
    BlynkRemoteStatus pending_{};
    BlynkRemoteStatus last_published_{};
    bool has_pending_{false};
    bool dirty_{false};
    bool connected_{false};
    bool publish_on_connect_{false};
    bool has_last_publish_{false};
    std::int64_t last_publish_ms_{0};
};

enum class BlynkCommandResultKind : std::uint8_t {
    SemanticAccepted,
    SemanticRejected,
    ServiceAccepted,
    ServiceRejected,
};

struct BlynkCommandFeedback final {
    std::uint32_t correlation_id{0U};
    BlynkCommandResultKind kind{BlynkCommandResultKind::SemanticRejected};
};

// A tracked ID reserves one slot before its action is admitted. Resolving the
// ID moves that reservation into feedback, so a full outbound backlog cannot
// make the semantic result depend on later snapshot retention. Disconnect
// drops both reservations and feedback.
class BlynkCommandResults final {
public:
    static constexpr std::size_t capacity = app::command_result_capacity;

    [[nodiscard]] bool track(std::uint32_t correlation_id) noexcept;
    [[nodiscard]] bool cancel(std::uint32_t correlation_id) noexcept;
    void observe(std::span<const app::CommandResultView> results) noexcept;
    [[nodiscard]] bool resolve_service_result(
        std::uint32_t correlation_id,
        bool accepted
    ) noexcept;
    [[nodiscard]] bool record_service_result(
        std::uint32_t correlation_id,
        bool accepted
    ) noexcept;
    [[nodiscard]] std::optional<BlynkCommandFeedback> pop() noexcept;
    void disconnected() noexcept;
    [[nodiscard]] std::size_t pending_count() const noexcept;

private:
    [[nodiscard]] bool enqueue(BlynkCommandFeedback feedback) noexcept;
    [[nodiscard]] bool resolve_tracked(BlynkCommandFeedback feedback) noexcept;

    std::array<std::uint32_t, capacity> pending_{};
    std::size_t pending_count_{0U};
    std::array<BlynkCommandFeedback, capacity> feedback_{};
    std::size_t feedback_head_{0U};
    std::size_t feedback_count_{0U};
};

[[nodiscard]] BlynkPayload serialize_blynk_command_feedback(
    BlynkCommandFeedback feedback
) noexcept;

enum class BlynkEventType : std::uint8_t {
    Fault,
    Alarm,
    SessionDone,
    Ota,
    RemoteError,
    Count,
};

struct BlynkEventMessage final {
    BlynkEventType type{BlynkEventType::RemoteError};
    std::array<char, 160U> description{};
};

[[nodiscard]] std::string_view blynk_event_code(BlynkEventType type) noexcept;

// Same-type events coalesce to the newest description and are separated by at
// least five seconds. Disconnect removes unpublished events.
class BlynkEventScheduler final {
public:
    void queue(BlynkEventType type, std::string_view description) noexcept;
    [[nodiscard]] std::optional<BlynkEventMessage> pending_publish(
        std::int64_t now_monotonic_ms
    ) const noexcept;
    void publish_succeeded(
        BlynkEventType type,
        std::int64_t now_monotonic_ms
    ) noexcept;
    void disconnected() noexcept;

private:
    static constexpr std::size_t type_count =
        static_cast<std::size_t>(BlynkEventType::Count);
    std::array<BlynkEventMessage, type_count> messages_{};
    std::array<bool, type_count> pending_{};
    std::array<bool, type_count> has_last_publish_{};
    std::array<std::int64_t, type_count> last_publish_ms_{};
};

} // namespace smoker::platform

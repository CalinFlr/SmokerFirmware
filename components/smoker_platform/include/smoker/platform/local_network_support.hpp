#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace smoker::platform {

enum class WifiSecurityCategory : std::uint8_t {
    Open,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
    Enterprise,
    Unknown,
};

[[nodiscard]] const char* wifi_security_category_name(WifiSecurityCategory category) noexcept;
[[nodiscard]] bool wifi_security_is_supported(WifiSecurityCategory category) noexcept;

enum class HttpRequestScope : std::uint8_t {
    Commissioning,
    Operational,
    Rejected,
};

constexpr std::uint32_t commissioning_ipv4 =
    (192U << 24U) | (168U << 16U) | (4U << 8U) | 1U;

[[nodiscard]] HttpRequestScope classify_http_request_scope(
    std::uint32_t local_ipv4,
    std::optional<std::uint32_t> sta_ipv4,
    bool commissioning_active
) noexcept;

// ESP-IDF may expose an accepted IPv4 HTTP socket as ::ffff:a.b.c.d when the
// server uses its dual-stack listener. Native IPv6 addresses remain rejected.
[[nodiscard]] std::optional<std::uint32_t> extract_ipv4_mapped_address(
    std::span<const std::uint8_t, 16U> address
) noexcept;

struct HttpErrorBody final {
    static constexpr std::size_t capacity = 192U;

    std::array<char, capacity> bytes{};
    std::size_t length{0U};
};

// Builds the bounded JSON error envelope used by low-memory HTTP paths without
// allocating. Invalid/unbounded messages are replaced by a fixed safe value.
[[nodiscard]] HttpErrorBody build_http_error_body(std::string_view message) noexcept;

struct HttpCommandAdmissionBody final {
    static constexpr std::size_t capacity = 96U;

    std::array<char, capacity> bytes{};
    std::size_t length{0U};
};

// Builds the fixed command-admission response before the command is published
// to the cross-task mailbox, so local JSON allocation failure cannot turn an
// admitted command into an HTTP 503 response.
[[nodiscard]] HttpCommandAdmissionBody build_http_command_admission_body(
    std::uint32_t correlation_id
) noexcept;

enum class LegacyPasswordState : std::uint8_t {
    Missing,
    Valid,
    Invalid,
};

enum class LegacyClaimState : std::uint8_t {
    Missing,
    Unclaimed,
    Claimed,
    Invalid,
};

enum class LegacyAuthenticationMigrationAction : std::uint8_t {
    UseInitial,
    PreserveInitial,
    PreserveClaimed,
    Reject,
};

// Decides migration without touching NVS. Invalid reads/corrupt values and a
// claimed marker without a valid password fail closed.
[[nodiscard]] LegacyAuthenticationMigrationAction
decide_legacy_authentication_migration(
    LegacyPasswordState password_state,
    bool password_matches_initial,
    LegacyClaimState claim_state
) noexcept;

struct RawWifiNetwork final {
    std::array<std::uint8_t, 32U> ssid{};
    std::size_t ssid_length{0U};
    std::int16_t rssi_dbm{0};
    std::uint8_t channel{0U};
    WifiSecurityCategory security{WifiSecurityCategory::Unknown};
};

struct WifiNetwork final {
    std::array<char, 33U> ssid{};
    std::int16_t rssi_dbm{0};
    std::uint8_t channel{0U};
    WifiSecurityCategory security{WifiSecurityCategory::Unknown};
};

struct WifiNetworkList final {
    static constexpr std::size_t capacity = 20U;

    std::array<WifiNetwork, capacity> networks{};
    std::size_t count{0U};
    bool truncated{false};
};

[[nodiscard]] WifiNetworkList curate_wifi_networks(
    std::span<const RawWifiNetwork> records
) noexcept;

// Matches an HTTP Host header against one device authority. A decimal port is
// optional, but prefixes, user-info, whitespace, and invalid ports are not.
[[nodiscard]] bool host_matches_device_authority(
    std::string_view host_header, std::string_view device_authority
) noexcept;

[[nodiscard]] bool http_content_type_matches(
    std::string_view provided, std::string_view expected_media_type
) noexcept;

// Every state-changing browser/API request requires an explicit exact Origin.
[[nodiscard]] bool http_origin_allowed(
    std::string_view host,
    std::optional<std::string_view> origin,
    bool state_changing
) noexcept;

struct HistorySessionsQuery final {
    std::optional<std::uint64_t> before;
    std::uint8_t limit{16U};
};

struct HistorySamplesQuery final {
    std::uint64_t history_id{0U};
    std::optional<std::uint32_t> after;
    std::uint8_t limit{60U};
    std::uint16_t stride{1U};
};

// Numeric history query strings are deliberately strict: no duplicate,
// unknown, empty, signed, encoded, overflowing, or out-of-range values.
[[nodiscard]] std::optional<HistorySessionsQuery> parse_history_sessions_query(
    std::string_view query
) noexcept;
[[nodiscard]] std::optional<HistorySamplesQuery> parse_history_samples_query(
    std::string_view query
) noexcept;

class HttpSessionState final {
public:
    static constexpr std::size_t token_characters = 64U;
    static constexpr std::int64_t idle_timeout_microseconds =
        30LL * 60LL * 1000LL * 1000LL;

    [[nodiscard]] bool replace(
        std::string_view token, std::int64_t now_microseconds
    ) noexcept;
    [[nodiscard]] bool validate(
        std::string_view token, std::int64_t now_microseconds
    ) noexcept;
    void invalidate() noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    std::array<char, token_characters + 1U> token_{};
    std::int64_t expiration_microseconds_{0};
    bool active_{false};
};

class LoginRateLimiter final {
public:
    static constexpr std::size_t peer_capacity = 4U;

    [[nodiscard]] bool permit(std::uint32_t peer, std::int64_t now_microseconds) noexcept;
    void record_failure(std::uint32_t peer, std::int64_t now_microseconds) noexcept;
    void record_success(std::uint32_t peer) noexcept;
    [[nodiscard]] std::uint32_t retry_after_seconds(
        std::uint32_t peer, std::int64_t now_microseconds
    ) const noexcept;

private:
    struct Entry final {
        std::uint32_t peer{0U};
        std::uint32_t failures{0U};
        std::int64_t last_attempt_microseconds{0};
        std::int64_t blocked_until_microseconds{0};
        bool used{false};
    };

    [[nodiscard]] Entry* find_or_allocate(
        std::uint32_t peer, std::int64_t now_microseconds
    ) noexcept;
    [[nodiscard]] const Entry* find(std::uint32_t peer) const noexcept;

    std::array<Entry, peer_capacity> entries_{};
};

enum class WifiScanState : std::uint8_t {
    Idle,
    Scanning,
    Complete,
    Error,
};

[[nodiscard]] const char* wifi_scan_state_name(WifiScanState state) noexcept;

class WifiScanCoordinator final {
public:
    struct Action final {
        bool start_scan{false};
        bool reconnect_sta{false};
    };

    [[nodiscard]] Action request(bool sta_configured, bool sta_connected) noexcept;
    void note_sta_disconnected(bool sta_configured) noexcept;
    [[nodiscard]] Action complete(bool sta_connected) noexcept;
    [[nodiscard]] Action fail(bool sta_connected) noexcept;

    [[nodiscard]] WifiScanState state() const noexcept;
    [[nodiscard]] bool blocks_sta_reconnect() const noexcept;

private:
    WifiScanState state_{WifiScanState::Idle};
    bool reconnect_after_scan_{false};
};

// Keeps the SoftAP recovery deadline anchored to the beginning of a STA
// outage. Repeated authentication failures must not postpone provisioning
// access indefinitely.
class WifiFallbackCoordinator final {
public:
    [[nodiscard]] bool arm(bool ap_active, bool sta_connected) noexcept;
    [[nodiscard]] bool expire(bool sta_connected) noexcept;
    [[nodiscard]] bool permit_enable(bool sta_connected) const noexcept;
    void cancel() noexcept;
    [[nodiscard]] bool armed() const noexcept;

private:
    bool armed_{false};
};

enum class WifiStaRetryAction : std::uint8_t {
    Connect,
    ReapplyConfiguration,
};

// A failed mode/configuration write must be retried before esp_wifi_connect();
// otherwise the driver can reconnect with stale credentials even though the
// new configuration was already persisted.
class WifiStaRetryState final {
public:
    void note_configuration_apply_failed() noexcept;
    void note_configuration_applied() noexcept;
    [[nodiscard]] WifiStaRetryAction action() const noexcept;

private:
    bool configuration_apply_pending_{false};
};

enum class DnsResponseStatus : std::uint8_t {
    Answer,
    NoAnswer,
    Invalid,
    Truncated,
};

struct DnsResponseResult final {
    DnsResponseStatus status{DnsResponseStatus::Invalid};
    std::size_t length{0U};
};

[[nodiscard]] DnsResponseResult build_captive_dns_response(
    std::span<const std::uint8_t> request,
    std::span<std::uint8_t> response,
    const std::array<std::uint8_t, 4U>& portal_ipv4
) noexcept;

} // namespace smoker::platform

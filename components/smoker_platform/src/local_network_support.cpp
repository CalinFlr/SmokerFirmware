#include "smoker/platform/local_network_support.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>

namespace smoker::platform {
namespace {

template <typename Integer>
std::optional<Integer> parse_decimal(const std::string_view text) noexcept
{
    if (text.empty()) return std::nullopt;
    Integer value = 0U;
    for (const char character : text) {
        if (character < '0' || character > '9') return std::nullopt;
        const auto digit = static_cast<Integer>(character - '0');
        if (value > (std::numeric_limits<Integer>::max() - digit) / 10U) {
            return std::nullopt;
        }
        value = static_cast<Integer>(value * 10U + digit);
    }
    return value;
}

template <typename Visitor>
bool visit_query(const std::string_view query, Visitor&& visitor) noexcept
{
    if (query.empty()) return true;
    std::size_t cursor = 0U;
    while (cursor < query.size()) {
        const auto separator = query.find('&', cursor);
        const auto end = separator == std::string_view::npos ? query.size() : separator;
        const auto field = query.substr(cursor, end - cursor);
        const auto equals = field.find('=');
        if (field.empty() || equals == std::string_view::npos
            || equals == 0U || equals + 1U == field.size()
            || field.find('=', equals + 1U) != std::string_view::npos
            || !visitor(field.substr(0U, equals), field.substr(equals + 1U))) {
            return false;
        }
        if (separator == std::string_view::npos) return true;
        cursor = separator + 1U;
        if (cursor == query.size()) return false;
    }
    return true;
}

constexpr std::uint16_t dns_type_a = 1U;
constexpr std::uint16_t dns_type_any = 255U;
constexpr std::uint16_t dns_class_in = 1U;
constexpr std::size_t dns_header_size = 12U;
constexpr std::size_t dns_answer_size = 16U;

std::uint16_t read_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) << 8U
        | static_cast<std::uint16_t>(bytes[offset + 1U])
    );
}

void write_u16(
    const std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint16_t value
) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

bool append_ssid_byte(
    std::array<char, 33U>& output,
    std::size_t& output_length,
    const std::uint8_t value
) noexcept
{
    if (output_length >= output.size() - 1U) {
        return false;
    }
    output[output_length++] = static_cast<char>(value);
    return true;
}

std::array<char, 33U> sanitize_ssid(const RawWifiNetwork& record) noexcept
{
    std::array<char, 33U> output{};
    const auto input_length = std::min(record.ssid_length, record.ssid.size());
    std::size_t input_offset = 0U;
    std::size_t output_length = 0U;
    while (input_offset < input_length) {
        const auto first = record.ssid[input_offset];
        if (first >= 0x20U && first <= 0x7EU) {
            static_cast<void>(append_ssid_byte(output, output_length, first));
            ++input_offset;
            continue;
        }

        std::size_t sequence_length = 0U;
        if (first >= 0xC2U && first <= 0xDFU) {
            sequence_length = 2U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            sequence_length = 3U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            sequence_length = 4U;
        }

        bool valid = sequence_length != 0U && input_offset + sequence_length <= input_length;
        for (std::size_t index = 1U; valid && index < sequence_length; ++index) {
            const auto continuation = record.ssid[input_offset + index];
            valid = continuation >= 0x80U && continuation <= 0xBFU;
        }
        if (valid && sequence_length == 3U) {
            const auto second = record.ssid[input_offset + 1U];
            valid = !((first == 0xE0U && second < 0xA0U)
                      || (first == 0xEDU && second >= 0xA0U));
        } else if (valid && sequence_length == 4U) {
            const auto second = record.ssid[input_offset + 1U];
            valid = !((first == 0xF0U && second < 0x90U)
                      || (first == 0xF4U && second > 0x8FU));
        }

        if (!valid || output_length + sequence_length > output.size() - 1U) {
            static_cast<void>(append_ssid_byte(output, output_length, static_cast<std::uint8_t>('?')));
            ++input_offset;
            continue;
        }
        for (std::size_t index = 0U; index < sequence_length; ++index) {
            static_cast<void>(append_ssid_byte(
                output, output_length, record.ssid[input_offset + index]
            ));
        }
        input_offset += sequence_length;
    }
    output[output_length] = '\0';
    return output;
}

bool ssid_equal(const WifiNetwork& left, const WifiNetwork& right) noexcept
{
    return std::strcmp(left.ssid.data(), right.ssid.data()) == 0;
}

std::string_view trim_ascii_whitespace(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1U);
    }
    return value;
}

bool ascii_case_equal(
    const std::string_view left, const std::string_view right
) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (std::tolower(left_byte) != std::tolower(right_byte)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool host_matches_device_authority(
    const std::string_view host_header, const std::string_view device_authority
) noexcept
{
    if (host_header.size() < device_authority.size() || device_authority.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < device_authority.size(); ++index) {
        const auto provided = static_cast<unsigned char>(host_header[index]);
        const auto expected = static_cast<unsigned char>(device_authority[index]);
        if (std::tolower(provided) != std::tolower(expected)) {
            return false;
        }
    }
    if (host_header.size() == device_authority.size()) {
        return true;
    }
    if (host_header[device_authority.size()] != ':') {
        return false;
    }
    const auto port = host_header.substr(device_authority.size() + 1U);
    if (port.empty() || port.size() > 5U) {
        return false;
    }
    std::uint32_t value = 0U;
    for (const char character : port) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < static_cast<unsigned char>('0')
            || byte > static_cast<unsigned char>('9')) {
            return false;
        }
        value = value * 10U + static_cast<std::uint32_t>(byte - '0');
    }
    return value > 0U && value <= 65535U;
}

const char* wifi_security_category_name(const WifiSecurityCategory category) noexcept
{
    switch (category) {
    case WifiSecurityCategory::Open: return "OPEN";
    case WifiSecurityCategory::Wep: return "WEP";
    case WifiSecurityCategory::Wpa: return "WPA";
    case WifiSecurityCategory::Wpa2: return "WPA2";
    case WifiSecurityCategory::Wpa3: return "WPA3";
    case WifiSecurityCategory::Enterprise: return "ENTERPRISE";
    case WifiSecurityCategory::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

bool wifi_security_is_supported(const WifiSecurityCategory category) noexcept
{
    return category == WifiSecurityCategory::Wpa2
        || category == WifiSecurityCategory::Wpa3;
}

HttpRequestScope classify_http_request_scope(
    const std::uint32_t local_ipv4,
    const std::optional<std::uint32_t> sta_ipv4,
    const bool commissioning_active
) noexcept
{
    if (local_ipv4 == commissioning_ipv4) {
        return commissioning_active
            ? HttpRequestScope::Commissioning
            : HttpRequestScope::Rejected;
    }
    // ESP-IDF's lwIP uses a weak-host receive model and may accept, on the AP
    // netif, a packet addressed to the STA netif. While the open commissioning
    // AP is active, a local STA destination address is therefore not proof that
    // the request entered through the protected LAN interface.
    if (!commissioning_active && sta_ipv4 && local_ipv4 == *sta_ipv4) {
        return HttpRequestScope::Operational;
    }
    return HttpRequestScope::Rejected;
}

std::optional<std::uint32_t> extract_ipv4_mapped_address(
    const std::span<const std::uint8_t, 16U> address
) noexcept
{
    for (std::size_t index = 0U; index < 10U; ++index) {
        if (address[index] != 0U) {
            return std::nullopt;
        }
    }
    if (address[10U] != 0xFFU || address[11U] != 0xFFU) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(address[12U]) << 24U
        | static_cast<std::uint32_t>(address[13U]) << 16U
        | static_cast<std::uint32_t>(address[14U]) << 8U
        | static_cast<std::uint32_t>(address[15U]);
}

HttpErrorBody build_http_error_body(const std::string_view message) noexcept
{
    constexpr std::string_view prefix = "{\"error\":\"";
    constexpr std::string_view suffix = "\"}";
    constexpr std::string_view fallback = "eroare internă";

    const bool requires_escaping = std::any_of(
        message.begin(), message.end(),
        [](const char character) {
            const auto byte = static_cast<unsigned char>(character);
            return character == '"' || character == '\\' || byte < 0x20U;
        }
    );
    const auto maximum_message_size = HttpErrorBody::capacity
        - prefix.size() - suffix.size() - 1U;
    const auto selected = !requires_escaping && message.size() <= maximum_message_size
        ? message
        : fallback;

    HttpErrorBody result;
    auto* output = result.bytes.data();
    std::memcpy(output, prefix.data(), prefix.size());
    output += prefix.size();
    std::memcpy(output, selected.data(), selected.size());
    output += selected.size();
    std::memcpy(output, suffix.data(), suffix.size());
    output += suffix.size();
    *output = '\0';
    result.length = static_cast<std::size_t>(output - result.bytes.data());
    return result;
}

HttpCommandAdmissionBody build_http_command_admission_body(
    const std::uint32_t correlation_id
) noexcept
{
    constexpr std::string_view prefix =
        "{\"status\":\"accepted\",\"coalesced_stop\":false,\"command_id\":";
    constexpr std::string_view suffix = "}";

    HttpCommandAdmissionBody result;
    auto* output = result.bytes.data();
    std::memcpy(output, prefix.data(), prefix.size());
    output += prefix.size();
    const auto conversion = std::to_chars(
        output,
        result.bytes.data() + result.bytes.size() - suffix.size() - 1U,
        correlation_id
    );
    if (conversion.ec != std::errc{}) {
        return {};
    }
    output = conversion.ptr;
    std::memcpy(output, suffix.data(), suffix.size());
    output += suffix.size();
    *output = '\0';
    result.length = static_cast<std::size_t>(output - result.bytes.data());
    return result;
}

LegacyAuthenticationMigrationAction decide_legacy_authentication_migration(
    const LegacyPasswordState password_state,
    const bool password_matches_initial,
    const LegacyClaimState claim_state
) noexcept
{
    if (password_state == LegacyPasswordState::Invalid
        || claim_state == LegacyClaimState::Invalid) {
        return LegacyAuthenticationMigrationAction::Reject;
    }
    if (password_state == LegacyPasswordState::Missing) {
        return claim_state == LegacyClaimState::Claimed
            ? LegacyAuthenticationMigrationAction::Reject
            : LegacyAuthenticationMigrationAction::UseInitial;
    }
    if (claim_state == LegacyClaimState::Unclaimed) {
        return LegacyAuthenticationMigrationAction::UseInitial;
    }
    if (claim_state == LegacyClaimState::Claimed) {
        return LegacyAuthenticationMigrationAction::PreserveClaimed;
    }
    return password_matches_initial
        ? LegacyAuthenticationMigrationAction::PreserveInitial
        : LegacyAuthenticationMigrationAction::PreserveClaimed;
}

bool http_content_type_matches(
    const std::string_view provided, const std::string_view expected_media_type
) noexcept
{
    const auto separator = provided.find(';');
    const auto media_type = trim_ascii_whitespace(provided.substr(0U, separator));
    if (!ascii_case_equal(media_type, expected_media_type)) {
        return false;
    }
    if (separator == std::string_view::npos) {
        return true;
    }
    return !trim_ascii_whitespace(provided.substr(separator + 1U)).empty();
}

bool http_origin_allowed(
    const std::string_view host,
    const std::optional<std::string_view> origin,
    const bool state_changing
) noexcept
{
    if (!origin) {
        return !state_changing;
    }
    constexpr std::string_view scheme = "http://";
    return origin->size() == scheme.size() + host.size()
        && origin->substr(0U, scheme.size()) == scheme
        && origin->substr(scheme.size()) == host;
}

bool HttpSessionState::replace(
    const std::string_view token, const std::int64_t now_microseconds
) noexcept
{
    if (token.size() != token_characters) {
        invalidate();
        return false;
    }
    token_.fill('\0');
    std::memcpy(token_.data(), token.data(), token.size());
    expiration_microseconds_ = now_microseconds + idle_timeout_microseconds;
    active_ = true;
    return true;
}

bool HttpSessionState::validate(
    const std::string_view token, const std::int64_t now_microseconds
) noexcept
{
    if (!active_ || now_microseconds >= expiration_microseconds_
        || token.size() != token_characters) {
        if (active_ && now_microseconds >= expiration_microseconds_) {
            invalidate();
        }
        return false;
    }
    unsigned char difference = 0U;
    for (std::size_t index = 0U; index < token_characters; ++index) {
        difference = static_cast<unsigned char>(
            difference
            | (static_cast<unsigned char>(token[index])
               ^ static_cast<unsigned char>(token_[index]))
        );
    }
    if (difference != 0U) {
        return false;
    }
    expiration_microseconds_ = now_microseconds + idle_timeout_microseconds;
    return true;
}

void HttpSessionState::invalidate() noexcept
{
    token_.fill('\0');
    expiration_microseconds_ = 0;
    active_ = false;
}

bool HttpSessionState::active() const noexcept
{
    return active_;
}

LoginRateLimiter::Entry* LoginRateLimiter::find_or_allocate(
    const std::uint32_t peer, const std::int64_t now_microseconds
) noexcept
{
    Entry* oldest = &entries_.front();
    for (auto& entry : entries_) {
        if (entry.used && entry.peer == peer) {
            return &entry;
        }
        if (!entry.used) {
            entry = Entry{peer, 0U, now_microseconds, 0, true};
            return &entry;
        }
        if (entry.last_attempt_microseconds < oldest->last_attempt_microseconds) {
            oldest = &entry;
        }
    }
    *oldest = Entry{peer, 0U, now_microseconds, 0, true};
    return oldest;
}

const LoginRateLimiter::Entry* LoginRateLimiter::find(
    const std::uint32_t peer
) const noexcept
{
    const auto entry = std::find_if(
        entries_.begin(), entries_.end(),
        [peer](const Entry& value) { return value.used && value.peer == peer; }
    );
    return entry == entries_.end() ? nullptr : &*entry;
}

bool LoginRateLimiter::permit(
    const std::uint32_t peer, const std::int64_t now_microseconds
) noexcept
{
    const auto* const entry = find(peer);
    return entry == nullptr || now_microseconds >= entry->blocked_until_microseconds;
}

void LoginRateLimiter::record_failure(
    const std::uint32_t peer, const std::int64_t now_microseconds
) noexcept
{
    constexpr std::int64_t reset_after = 10LL * 60LL * 1000LL * 1000LL;
    constexpr std::uint32_t failures_before_block = 5U;
    constexpr std::int64_t initial_block = 30LL * 1000LL * 1000LL;
    constexpr std::int64_t maximum_block = 5LL * 60LL * 1000LL * 1000LL;

    auto* const entry = find_or_allocate(peer, now_microseconds);
    if (now_microseconds - entry->last_attempt_microseconds >= reset_after) {
        entry->failures = 0U;
    }
    entry->last_attempt_microseconds = now_microseconds;
    if (entry->failures < std::numeric_limits<std::uint32_t>::max()) {
        ++entry->failures;
    }
    if (entry->failures < failures_before_block) {
        return;
    }
    const auto exponent = std::min<std::uint32_t>(
        entry->failures - failures_before_block, std::uint32_t{3U}
    );
    const auto block = std::min(initial_block << exponent, maximum_block);
    entry->blocked_until_microseconds = now_microseconds + block;
}

void LoginRateLimiter::record_success(const std::uint32_t peer) noexcept
{
    for (auto& entry : entries_) {
        if (entry.used && entry.peer == peer) {
            entry = {};
            return;
        }
    }
}

std::uint32_t LoginRateLimiter::retry_after_seconds(
    const std::uint32_t peer, const std::int64_t now_microseconds
) const noexcept
{
    const auto* const entry = find(peer);
    if (entry == nullptr || now_microseconds >= entry->blocked_until_microseconds) {
        return 0U;
    }
    constexpr std::int64_t microseconds_per_second = 1000LL * 1000LL;
    const auto remaining = entry->blocked_until_microseconds - now_microseconds;
    return static_cast<std::uint32_t>(
        (remaining + microseconds_per_second - 1LL) / microseconds_per_second
    );
}

WifiNetworkList curate_wifi_networks(const std::span<const RawWifiNetwork> records) noexcept
{
    std::array<WifiNetwork, WifiNetworkList::capacity> strongest{};
    std::size_t strongest_count = 0U;
    bool discarded = false;

    for (const auto& record : records) {
        if (record.ssid_length == 0U) {
            continue;
        }
        WifiNetwork candidate{
            sanitize_ssid(record), record.rssi_dbm, record.channel, record.security
        };
        if (candidate.ssid[0] == '\0') {
            continue;
        }

        auto duplicate = strongest.end();
        for (auto iterator = strongest.begin(); iterator != strongest.begin()
                 + static_cast<std::ptrdiff_t>(strongest_count); ++iterator) {
            if (ssid_equal(*iterator, candidate)) {
                duplicate = iterator;
                break;
            }
        }
        if (duplicate != strongest.end()) {
            if (candidate.rssi_dbm > duplicate->rssi_dbm) {
                *duplicate = candidate;
            }
            continue;
        }

        if (strongest_count < strongest.size()) {
            strongest[strongest_count++] = candidate;
            continue;
        }

        auto weakest = std::min_element(
            strongest.begin(), strongest.end(),
            [](const WifiNetwork& left, const WifiNetwork& right) {
                return left.rssi_dbm < right.rssi_dbm;
            }
        );
        if (candidate.rssi_dbm > weakest->rssi_dbm) {
            *weakest = candidate;
        }
        discarded = true;
    }

    std::sort(
        strongest.begin(), strongest.begin() + static_cast<std::ptrdiff_t>(strongest_count),
        [](const WifiNetwork& left, const WifiNetwork& right) {
            if (left.rssi_dbm != right.rssi_dbm) {
                return left.rssi_dbm > right.rssi_dbm;
            }
            return std::strcmp(left.ssid.data(), right.ssid.data()) < 0;
        }
    );

    WifiNetworkList result;
    result.networks = strongest;
    result.count = strongest_count;
    result.truncated = discarded;
    return result;
}

const char* wifi_scan_state_name(const WifiScanState state) noexcept
{
    switch (state) {
    case WifiScanState::Idle: return "idle";
    case WifiScanState::Scanning: return "scanning";
    case WifiScanState::Complete: return "complete";
    case WifiScanState::Error: return "error";
    }
    return "error";
}

WifiScanCoordinator::Action WifiScanCoordinator::request(
    const bool sta_configured, const bool sta_connected
) noexcept
{
    if (state_ == WifiScanState::Scanning) {
        return {};
    }
    state_ = WifiScanState::Scanning;
    reconnect_after_scan_ = sta_configured && !sta_connected;
    return Action{true, false};
}

void WifiScanCoordinator::note_sta_disconnected(const bool sta_configured) noexcept
{
    if (state_ == WifiScanState::Scanning && sta_configured) {
        reconnect_after_scan_ = true;
    }
}

WifiScanCoordinator::Action WifiScanCoordinator::complete(const bool sta_connected) noexcept
{
    const bool reconnect = reconnect_after_scan_ && !sta_connected;
    reconnect_after_scan_ = false;
    state_ = WifiScanState::Complete;
    return Action{false, reconnect};
}

WifiScanCoordinator::Action WifiScanCoordinator::fail(const bool sta_connected) noexcept
{
    const bool reconnect = reconnect_after_scan_ && !sta_connected;
    reconnect_after_scan_ = false;
    state_ = WifiScanState::Error;
    return Action{false, reconnect};
}

WifiScanState WifiScanCoordinator::state() const noexcept
{
    return state_;
}

bool WifiScanCoordinator::blocks_sta_reconnect() const noexcept
{
    return state_ == WifiScanState::Scanning;
}

bool WifiFallbackCoordinator::arm(
    const bool ap_active, const bool sta_connected
) noexcept
{
    if (armed_ || ap_active || sta_connected) {
        return false;
    }
    armed_ = true;
    return true;
}

bool WifiFallbackCoordinator::expire(const bool sta_connected) noexcept
{
    if (!armed_) {
        return false;
    }
    armed_ = false;
    return !sta_connected;
}

bool WifiFallbackCoordinator::permit_enable(const bool sta_connected) const noexcept
{
    return !sta_connected;
}

void WifiFallbackCoordinator::cancel() noexcept
{
    armed_ = false;
}

bool WifiFallbackCoordinator::armed() const noexcept
{
    return armed_;
}

void WifiStaRetryState::note_configuration_apply_failed() noexcept
{
    configuration_apply_pending_ = true;
}

void WifiStaRetryState::note_configuration_applied() noexcept
{
    configuration_apply_pending_ = false;
}

WifiStaRetryAction WifiStaRetryState::action() const noexcept
{
    return configuration_apply_pending_
        ? WifiStaRetryAction::ReapplyConfiguration
        : WifiStaRetryAction::Connect;
}

DnsResponseResult build_captive_dns_response(
    const std::span<const std::uint8_t> request,
    const std::span<std::uint8_t> response,
    const std::array<std::uint8_t, 4U>& portal_ipv4
) noexcept
{
    if (request.size() < dns_header_size) {
        return {DnsResponseStatus::Truncated, 0U};
    }
    if (read_u16(request, 4U) != 1U || (read_u16(request, 2U) & 0xF800U) != 0U) {
        return {DnsResponseStatus::Invalid, 0U};
    }

    std::size_t offset = dns_header_size;
    while (true) {
        if (offset >= request.size()) {
            return {DnsResponseStatus::Truncated, 0U};
        }
        const auto label_length = request[offset++];
        if (label_length == 0U) {
            break;
        }
        if ((label_length & 0xC0U) != 0U || label_length > 63U) {
            return {DnsResponseStatus::Invalid, 0U};
        }
        if (offset + label_length > request.size()) {
            return {DnsResponseStatus::Truncated, 0U};
        }
        offset += label_length;
    }
    if (offset + 4U > request.size()) {
        return {DnsResponseStatus::Truncated, 0U};
    }

    const auto question_type = read_u16(request, offset);
    const auto question_class = read_u16(request, offset + 2U);
    const auto question_end = offset + 4U;
    const bool answer = question_class == dns_class_in
        && (question_type == dns_type_a || question_type == dns_type_any);
    const auto required = question_end + (answer ? dns_answer_size : 0U);
    if (required > response.size()) {
        return {DnsResponseStatus::Truncated, 0U};
    }

    std::copy_n(request.begin(), static_cast<std::ptrdiff_t>(question_end), response.begin());
    const auto query_flags = read_u16(request, 2U);
    write_u16(response, 2U, static_cast<std::uint16_t>(0x8400U | (query_flags & 0x0100U)));
    write_u16(response, 6U, answer ? 1U : 0U);
    write_u16(response, 8U, 0U);
    write_u16(response, 10U, 0U);

    if (!answer) {
        return {DnsResponseStatus::NoAnswer, question_end};
    }
    write_u16(response, question_end, 0xC00CU);
    write_u16(response, question_end + 2U, dns_type_a);
    write_u16(response, question_end + 4U, dns_class_in);
    response[question_end + 6U] = 0U;
    response[question_end + 7U] = 0U;
    response[question_end + 8U] = 0U;
    response[question_end + 9U] = 0U;
    write_u16(response, question_end + 10U, 4U);
    std::copy(portal_ipv4.begin(), portal_ipv4.end(), response.begin()
              + static_cast<std::ptrdiff_t>(question_end + 12U));
    return {DnsResponseStatus::Answer, required};
}

std::optional<HistorySessionsQuery> parse_history_sessions_query(
    const std::string_view query
) noexcept
{
    HistorySessionsQuery result;
    bool saw_before = false;
    bool saw_limit = false;
    const bool valid = visit_query(query, [&](const auto name, const auto value) {
        if (name == "before" && !saw_before) {
            const auto parsed = parse_decimal<std::uint64_t>(value);
            if (!parsed || *parsed == 0U) return false;
            result.before = *parsed;
            saw_before = true;
            return true;
        }
        if (name == "limit" && !saw_limit) {
            const auto parsed = parse_decimal<std::uint8_t>(value);
            if (!parsed || *parsed < 1U || *parsed > 32U) return false;
            result.limit = *parsed;
            saw_limit = true;
            return true;
        }
        return false;
    });
    return valid ? std::optional<HistorySessionsQuery>{result} : std::nullopt;
}

std::optional<HistorySamplesQuery> parse_history_samples_query(
    const std::string_view query
) noexcept
{
    HistorySamplesQuery result;
    bool saw_history_id = false;
    bool saw_after = false;
    bool saw_limit = false;
    bool saw_stride = false;
    const bool valid = visit_query(query, [&](const auto name, const auto value) {
        if (name == "history_id" && !saw_history_id) {
            const auto parsed = parse_decimal<std::uint64_t>(value);
            if (!parsed || *parsed == 0U) return false;
            result.history_id = *parsed;
            saw_history_id = true;
            return true;
        }
        if (name == "after" && !saw_after) {
            const auto parsed = parse_decimal<std::uint32_t>(value);
            if (!parsed) return false;
            result.after = *parsed;
            saw_after = true;
            return true;
        }
        if (name == "limit" && !saw_limit) {
            const auto parsed = parse_decimal<std::uint8_t>(value);
            if (!parsed || *parsed < 1U || *parsed > 60U) return false;
            result.limit = *parsed;
            saw_limit = true;
            return true;
        }
        if (name == "stride" && !saw_stride) {
            const auto parsed = parse_decimal<std::uint16_t>(value);
            if (!parsed || *parsed < 1U) return false;
            result.stride = *parsed;
            saw_stride = true;
            return true;
        }
        return false;
    });
    return valid && saw_history_id
        ? std::optional<HistorySamplesQuery>{result} : std::nullopt;
}

} // namespace smoker::platform

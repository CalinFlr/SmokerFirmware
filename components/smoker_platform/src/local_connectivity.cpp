#include "smoker/platform/local_connectivity.hpp"
#include "smoker/platform/local_network_support.hpp"

#include "web_assets.hpp"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

namespace smoker::platform {
namespace {

constexpr char tag[] = "smoker_net";
constexpr char nvs_namespace[] = "smoker_net";
constexpr char nvs_wifi_configuration_key[] = "sta_config_v1";
constexpr char nvs_authentication_configuration_key[] = "auth_config_v1";
// Legacy M12 keys are read only while migrating an existing deployment.
constexpr char nvs_ssid_key[] = "sta_ssid";
constexpr char nvs_wifi_password_key[] = "sta_pass";
constexpr char nvs_device_password_key[] = "dev_pass";
constexpr char nvs_device_claimed_key[] = "dev_claimed";
constexpr char initial_device_password[] = "smoker257500";
constexpr std::size_t maximum_body_bytes = 512U;
constexpr std::string_view firmware_check_accepted_body =
    R"JSON({"status":"accepted"})JSON";
constexpr std::size_t maximum_body_receive_timeouts = 1U;
constexpr std::int64_t body_receive_deadline_microseconds = 10LL * 1000LL * 1000LL;
constexpr std::uint64_t fallback_delay_microseconds = 30ULL * 1000ULL * 1000ULL;
constexpr std::uint64_t sta_only_retry_microseconds = 500ULL * 1000ULL;
constexpr std::uint64_t sta_connect_retry_microseconds = 1000ULL * 1000ULL;
constexpr std::uint64_t scan_timeout_microseconds = 15ULL * 1000ULL * 1000ULL;
constexpr char captive_portal_uri[] = "http://192.168.4.1/";
constexpr char session_cookie_name[] = "smoker_session";
constexpr std::size_t maximum_scan_records = 40U;
constexpr std::size_t dns_packet_bytes = 512U;
constexpr std::size_t dns_stack_bytes = 4096U;

struct NetworkConfiguration final {
    std::array<char, 33U> ssid{};
    std::array<char, 65U> wifi_password{};
    std::array<char, 64U> device_password{};
    bool has_sta_credentials{false};
    bool device_password_is_initial{true};
};

struct NetworkStatus final {
    bool sta_connected{false};
    bool ap_active{false};
    std::uint32_t sta_ipv4{0U};
    std::array<char, 16U> sta_ip{};
    std::array<char, 32U> sta_last_error{};
};

constexpr std::array<std::uint8_t, 4U> persisted_wifi_magic{
    'S', 'W', 'F', '1'
};
constexpr std::array<std::uint8_t, 4U> persisted_authentication_magic{
    'S', 'A', 'U', '1'
};
constexpr std::uint8_t persisted_configuration_version = 1U;

// Byte-aligned, fixed-size payloads keep each independent credential concern
// in one NVS entry. A successful nvs_set_blob() therefore cannot expose a new
// SSID with an old password, or a new device password with an old claim flag.
struct PersistedWifiConfiguration final {
    std::array<std::uint8_t, 4U> magic{};
    std::uint8_t version{0U};
    std::array<char, 33U> ssid{};
    std::array<char, 65U> wifi_password{};
};

struct PersistedAuthenticationConfiguration final {
    std::array<std::uint8_t, 4U> magic{};
    std::uint8_t version{0U};
    std::uint8_t device_password_is_initial{1U};
    std::array<char, 64U> device_password{};
};

static_assert(sizeof(PersistedWifiConfiguration) == 103U);
static_assert(sizeof(PersistedAuthenticationConfiguration) == 70U);

enum class StoredStringStatus : std::uint8_t {
    Missing,
    Loaded,
    Invalid,
};

enum class StoredBlobStatus : std::uint8_t {
    Missing,
    Loaded,
    Invalid,
};

struct CjsonDeleter final {
    void operator()(cJSON* const value) const noexcept
    {
        cJSON_Delete(value);
    }
};

using CjsonPointer = std::unique_ptr<cJSON, CjsonDeleter>;

template <std::size_t Size>
bool copy_bounded(std::array<char, Size>& destination, const std::string_view source) noexcept
{
    if (source.size() >= destination.size()) {
        return false;
    }
    destination.fill('\0');
    std::memcpy(destination.data(), source.data(), source.size());
    return true;
}

template <std::size_t Size>
std::optional<std::size_t> bounded_string_length(
    const std::array<char, Size>& value
) noexcept
{
    const auto terminator = std::find(value.begin(), value.end(), '\0');
    if (terminator == value.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(terminator - value.begin());
}

PersistedWifiConfiguration encode_wifi_configuration(
    const NetworkConfiguration& configuration
) noexcept
{
    PersistedWifiConfiguration persisted;
    persisted.magic = persisted_wifi_magic;
    persisted.version = persisted_configuration_version;
    persisted.ssid = configuration.ssid;
    persisted.wifi_password = configuration.wifi_password;
    return persisted;
}

bool decode_wifi_configuration(
    const PersistedWifiConfiguration& persisted,
    NetworkConfiguration& configuration
) noexcept
{
    if (persisted.magic != persisted_wifi_magic
        || persisted.version != persisted_configuration_version) {
        return false;
    }
    const auto ssid_length = bounded_string_length(persisted.ssid);
    const auto password_length = bounded_string_length(persisted.wifi_password);
    if (!ssid_length || !password_length) {
        return false;
    }
    const bool unconfigured = *ssid_length == 0U && *password_length == 0U;
    const bool configured = *ssid_length >= 1U && *ssid_length <= 32U
        && *password_length >= 8U && *password_length <= 63U;
    if (!unconfigured && !configured) {
        return false;
    }
    configuration.ssid = persisted.ssid;
    configuration.wifi_password = persisted.wifi_password;
    configuration.has_sta_credentials = configured;
    return true;
}

PersistedAuthenticationConfiguration encode_authentication_configuration(
    const NetworkConfiguration& configuration
) noexcept
{
    PersistedAuthenticationConfiguration persisted;
    persisted.magic = persisted_authentication_magic;
    persisted.version = persisted_configuration_version;
    persisted.device_password_is_initial =
        configuration.device_password_is_initial ? 1U : 0U;
    persisted.device_password = configuration.device_password;
    return persisted;
}

bool decode_authentication_configuration(
    const PersistedAuthenticationConfiguration& persisted,
    NetworkConfiguration& configuration
) noexcept
{
    if (persisted.magic != persisted_authentication_magic
        || persisted.version != persisted_configuration_version
        || persisted.device_password_is_initial > 1U) {
        return false;
    }
    const auto password_length = bounded_string_length(persisted.device_password);
    if (!password_length || *password_length < 8U || *password_length > 63U) {
        return false;
    }
    configuration.device_password = persisted.device_password;
    configuration.device_password_is_initial =
        persisted.device_password_is_initial != 0U;
    return true;
}

bool constant_time_equal(const std::string_view left, const std::string_view right) noexcept
{
    std::size_t difference = left.size() ^ right.size();
    const auto length = std::max(left.size(), right.size());
    for (std::size_t index = 0U; index < length; ++index) {
        const auto left_byte = index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const auto right_byte = index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= static_cast<std::size_t>(left_byte ^ right_byte);
    }
    return difference == 0U;
}

std::optional<std::uint8_t> hexadecimal_value(const char value) noexcept
{
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    return std::nullopt;
}

template <std::size_t Size>
bool decode_form_component(
    const std::string_view encoded, std::array<char, Size>& decoded
) noexcept
{
    decoded.fill('\0');
    std::size_t output = 0U;
    for (std::size_t input = 0U; input < encoded.size(); ++input) {
        unsigned char value = static_cast<unsigned char>(encoded[input]);
        if (value == static_cast<unsigned char>('+')) {
            value = static_cast<unsigned char>(' ');
        } else if (value == static_cast<unsigned char>('%')) {
            if (input + 2U >= encoded.size()) {
                return false;
            }
            const auto high = hexadecimal_value(encoded[input + 1U]);
            const auto low = hexadecimal_value(encoded[input + 2U]);
            if (!high || !low) {
                return false;
            }
            value = static_cast<unsigned char>((*high << 4U) | *low);
            input += 2U;
        }
        if (value == 0U || output + 1U >= decoded.size()) {
            return false;
        }
        decoded[output++] = static_cast<char>(value);
    }
    return true;
}

const char* session_status_name(const core::SessionStatus status) noexcept
{
    switch (status) {
    case core::SessionStatus::Idle: return "IDLE";
    case core::SessionStatus::Running: return "RUNNING";
    case core::SessionStatus::Stopped: return "STOPPED";
    case core::SessionStatus::Fault: return "FAULT";
    }
    return "IDLE";
}

const char* stop_reason_name(const core::StopReason reason) noexcept
{
    switch (reason) {
    case core::StopReason::None: return "NONE";
    case core::StopReason::User: return "USER";
    case core::StopReason::TimerCompleted: return "TIMER_COMPLETED";
    case core::StopReason::Fault: return "FAULT";
    case core::StopReason::RecoveryNotAllowed: return "RECOVERY_NOT_ALLOWED";
    }
    return "NONE";
}

const char* probe_role_name(const core::ProbeRole role) noexcept
{
    switch (role) {
    case core::ProbeRole::Meat: return "MEAT";
    case core::ProbeRole::AmbientMonitor: return "AMBIENT_MONITOR";
    case core::ProbeRole::Unassigned: return "UNASSIGNED";
    }
    return "UNASSIGNED";
}

const char* alarm_code_name(const core::AlarmCode code) noexcept
{
    switch (code) {
    case core::AlarmCode::ProbeTargetReached: return "PROBE_TARGET_REACHED";
    case core::AlarmCode::ProbeDisconnected: return "PROBE_DISCONNECTED";
    case core::AlarmCode::TimerCompleted: return "TIMER_COMPLETED";
    }
    return "TIMER_COMPLETED";
}

const char* fault_code_name(const core::FaultCode code) noexcept
{
    switch (code) {
    case core::FaultCode::ChamberSensorInvalid: return "CHAMBER_SENSOR_INVALID";
    case core::FaultCode::ChamberOverTemperature: return "CHAMBER_OVER_TEMPERATURE";
    case core::FaultCode::ControlLoopFailure: return "CONTROL_LOOP_FAILURE";
    case core::FaultCode::ConfigurationInvalid: return "CONFIGURATION_INVALID";
    }
    return "CONFIGURATION_INVALID";
}

bool add_optional_temperature(
    cJSON* const object,
    const char* const name,
    const std::optional<core::Temperature>& value
)
{
    if (value) {
        return cJSON_AddNumberToObject(
            object, name, static_cast<double>(value->celsius())
        ) != nullptr;
    }
    return cJSON_AddNullToObject(object, name) != nullptr;
}

template <typename Value>
Value* require_json(Value* const value, bool& valid) noexcept
{
    valid = valid && value != nullptr;
    return value;
}

void require_json(const cJSON_bool value, bool& valid) noexcept
{
    valid = valid && value != 0;
}

bool has_exact_fields(
    const cJSON* const object,
    const std::initializer_list<const char*> allowed,
    const std::initializer_list<const char*> required
) noexcept
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    for (const cJSON* field = object->child; field != nullptr; field = field->next) {
        if (field->string == nullptr) {
            return false;
        }
        bool known = false;
        std::size_t occurrences = 0U;
        for (const char* const name : allowed) {
            if (std::strcmp(field->string, name) == 0) {
                known = true;
                for (const cJSON* candidate = object->child; candidate != nullptr;
                     candidate = candidate->next) {
                    if (candidate->string != nullptr
                        && std::strcmp(candidate->string, name) == 0) {
                        ++occurrences;
                    }
                }
                break;
            }
        }
        if (!known || occurrences != 1U) {
            return false;
        }
    }
    for (const char* const name : required) {
        if (cJSON_GetObjectItemCaseSensitive(object, name) == nullptr) {
            return false;
        }
    }
    return true;
}

std::optional<core::Temperature> parse_temperature_or_null(
    const cJSON* const value, bool& valid
) noexcept
{
    if (cJSON_IsNull(value)) {
        valid = true;
        return std::nullopt;
    }
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)
        || value->valuedouble < -std::numeric_limits<float>::max()
        || value->valuedouble > std::numeric_limits<float>::max()) {
        valid = false;
        return std::nullopt;
    }
    const auto temperature = core::Temperature::from_celsius(
        static_cast<float>(value->valuedouble)
    );
    valid = temperature.has_value();
    return temperature;
}

WifiSecurityCategory security_category(const wifi_auth_mode_t authentication) noexcept
{
    switch (authentication) {
    case WIFI_AUTH_OPEN: return WifiSecurityCategory::Open;
    case WIFI_AUTH_WEP: return WifiSecurityCategory::Wep;
    case WIFI_AUTH_WPA_PSK: return WifiSecurityCategory::Wpa;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK: return WifiSecurityCategory::Wpa2;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: return WifiSecurityCategory::Wpa3;
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE: return WifiSecurityCategory::Enterprise;
    case WIFI_AUTH_WAPI_PSK:
    case WIFI_AUTH_OWE:
    case WIFI_AUTH_DPP:
    case WIFI_AUTH_DUMMY_1:
    case WIFI_AUTH_DUMMY_2:
    case WIFI_AUTH_MAX: return WifiSecurityCategory::Unknown;
    }
    return WifiSecurityCategory::Unknown;
}

const char* sta_disconnect_error(const std::uint8_t reason) noexcept
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND: return "network_not_found";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "signal_too_weak";
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "authentication_or_security_failed";
    case WIFI_REASON_ASSOC_LEAVE:
    case WIFI_REASON_AUTH_LEAVE: return "configuration_changed";
    default: return "connection_lost";
    }
}

template <typename Integer>
std::optional<Integer> parse_unsigned_integer(const cJSON* const value) noexcept
{
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)
        || value->valuedouble < 0.0 || std::floor(value->valuedouble) != value->valuedouble
        || value->valuedouble > static_cast<double>(std::numeric_limits<Integer>::max())) {
        return std::nullopt;
    }
    return static_cast<Integer>(value->valuedouble);
}

} // namespace

class LocalConnectivityService::Impl final {
public:
    Impl(
        app::SpscCommandMailbox& command_mailbox,
        const app::SnapshotExchange& snapshots,
        FirmwareUpdateService& firmware_updates,
        core::Recipe startup_recipe
    )
        : command_mailbox_{command_mailbox}
        , snapshots_{snapshots}
        , firmware_updates_{firmware_updates}
        , startup_recipe_{std::move(startup_recipe)}
    {
    }

    ~Impl()
    {
        stop_dns_responder();
        if (http_server_ != nullptr) {
            static_cast<void>(httpd_stop(http_server_));
        }
        if (fallback_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(fallback_timer_));
            static_cast<void>(esp_timer_delete(fallback_timer_));
        }
        if (sta_only_retry_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(sta_only_retry_timer_));
            static_cast<void>(esp_timer_delete(sta_only_retry_timer_));
        }
        if (sta_connect_retry_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(sta_connect_retry_timer_));
            static_cast<void>(esp_timer_delete(sta_connect_retry_timer_));
        }
        if (scan_timeout_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(scan_timeout_timer_));
            static_cast<void>(esp_timer_delete(scan_timeout_timer_));
        }
        if (wifi_handler_ != nullptr) {
            static_cast<void>(esp_event_handler_instance_unregister(
                WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_
            ));
        }
        if (ip_handler_ != nullptr) {
            static_cast<void>(esp_event_handler_instance_unregister(
                IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler_
            ));
        }
        if (wifi_started_) {
            static_cast<void>(esp_wifi_stop());
            static_cast<void>(esp_wifi_deinit());
        }
        if (mdns_started_) {
            mdns_free();
        }
        if (nvs_handle_ != 0U) {
            nvs_close(nvs_handle_);
        }
    }

    [[nodiscard]] bool start() noexcept
    {
        if (!initialize_nvs() || !initialize_identity() || !initialize_network_stack()
            || !initialize_http()) {
            return false;
        }

        if (configuration_.has_sta_credentials) {
            if (!start_sta()) {
                ESP_LOGW(tag, "STA startup failed; exposing SoftAP recovery");
                if (!enable_soft_ap(false)) {
                    return false;
                }
            } else {
                schedule_fallback();
            }
        } else if (!enable_soft_ap(false)) {
            return false;
        }

        ESP_LOGI(
            tag,
            "HTTP local password/session auth; initial password warning=%s; TLS=off NVS encryption=off",
            default_password_warning() ? "active" : "cleared"
        );
        if (configuration_.device_password_is_initial) {
            ESP_LOGW(
                tag,
                "Initial HTTP device password (change after LAN login): %s",
                configuration_.device_password.data()
            );
        }
        return true;
    }

    void mark_control_ready() noexcept
    {
        control_ready_.store(true, std::memory_order_release);
    }

private:
    [[nodiscard]] bool initialize_nvs() noexcept
    {
        const auto flash_status = nvs_flash_init();
        if (flash_status != ESP_OK) {
            ESP_LOGE(
                tag,
                "NVS init failed without automatic erase: %s",
                esp_err_to_name(flash_status)
            );
            return false;
        }
        const auto open_status = nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle_);
        if (open_status != ESP_OK) {
            ESP_LOGE(tag, "NVS namespace open failed: %s", esp_err_to_name(open_status));
            return false;
        }

        PersistedWifiConfiguration persisted_wifi;
        const auto wifi_blob_status = load_blob(
            nvs_wifi_configuration_key, persisted_wifi
        );
        bool persist_wifi_blob = false;
        if (wifi_blob_status == StoredBlobStatus::Loaded) {
            if (!decode_wifi_configuration(persisted_wifi, configuration_)) {
                ESP_LOGE(tag, "Stored Wi-Fi configuration blob is invalid");
                return false;
            }
        } else if (wifi_blob_status == StoredBlobStatus::Invalid) {
            ESP_LOGE(tag, "Could not read stored Wi-Fi configuration blob");
            return false;
        } else {
            const auto ssid_status = load_string(nvs_ssid_key, configuration_.ssid);
            const auto password_status = load_string(
                nvs_wifi_password_key, configuration_.wifi_password
            );
            if (ssid_status == StoredStringStatus::Invalid
                || password_status == StoredStringStatus::Invalid) {
                ESP_LOGE(tag, "Could not read legacy Wi-Fi configuration");
                return false;
            }
            const auto ssid_length = std::strlen(configuration_.ssid.data());
            const auto wifi_password_length = std::strlen(
                configuration_.wifi_password.data()
            );
            configuration_.has_sta_credentials = ssid_length > 0U
                && wifi_password_length >= 8U && wifi_password_length <= 63U;
            if (!configuration_.has_sta_credentials) {
                configuration_.ssid.fill('\0');
                configuration_.wifi_password.fill('\0');
            }
            persist_wifi_blob = true;
        }

        PersistedAuthenticationConfiguration persisted_authentication;
        const auto authentication_blob_status = load_blob(
            nvs_authentication_configuration_key, persisted_authentication
        );
        bool persist_authentication_blob = false;
        if (authentication_blob_status == StoredBlobStatus::Loaded) {
            if (!decode_authentication_configuration(
                    persisted_authentication, configuration_
                )) {
                ESP_LOGE(tag, "Stored device-authentication blob is invalid");
                return false;
            }
        } else if (authentication_blob_status == StoredBlobStatus::Invalid) {
            ESP_LOGE(tag, "Could not read stored device-authentication blob");
            return false;
        } else {
            const auto device_password_status = load_string(
                nvs_device_password_key, configuration_.device_password
            );
            const auto device_password_length = std::strlen(
                configuration_.device_password.data()
            );
            std::uint8_t claimed = 0U;
            const auto claimed_status = nvs_get_u8(
                nvs_handle_, nvs_device_claimed_key, &claimed
            );
            const bool device_password_valid =
                device_password_status == StoredStringStatus::Loaded
                && device_password_length >= 8U && device_password_length <= 63U;

            LegacyPasswordState password_state = LegacyPasswordState::Invalid;
            if (device_password_status == StoredStringStatus::Missing) {
                password_state = LegacyPasswordState::Missing;
            } else if (device_password_valid) {
                password_state = LegacyPasswordState::Valid;
            }
            LegacyClaimState claim_state = LegacyClaimState::Invalid;
            if (claimed_status == ESP_ERR_NVS_NOT_FOUND) {
                claim_state = LegacyClaimState::Missing;
            } else if (claimed_status == ESP_OK && claimed == 0U) {
                claim_state = LegacyClaimState::Unclaimed;
            } else if (claimed_status == ESP_OK && claimed == 1U) {
                claim_state = LegacyClaimState::Claimed;
            }

            const auto migration = decide_legacy_authentication_migration(
                password_state,
                device_password_valid
                    && std::strcmp(
                           configuration_.device_password.data(),
                           initial_device_password
                       ) == 0,
                claim_state
            );
            if (migration == LegacyAuthenticationMigrationAction::Reject) {
                ESP_LOGE(
                    tag,
                    "Legacy device-authentication state is invalid or unreadable"
                );
                return false;
            }
            if (migration == LegacyAuthenticationMigrationAction::UseInitial) {
                static_cast<void>(copy_bounded(
                    configuration_.device_password, initial_device_password
                ));
                configuration_.device_password_is_initial = true;
                ESP_LOGW(tag, "Using product-required initial HTTP device password");
            } else {
                configuration_.device_password_is_initial =
                    migration
                    == LegacyAuthenticationMigrationAction::PreserveInitial;
            }
            persist_authentication_blob = true;
        }

        if (persist_wifi_blob && !persist_wifi_configuration(configuration_)) {
            ESP_LOGE(tag, "Could not migrate atomic Wi-Fi configuration");
            return false;
        }
        if (persist_authentication_blob
            && !persist_authentication_configuration(configuration_)) {
            ESP_LOGE(tag, "Could not migrate atomic device authentication");
            return false;
        }
        if (persist_wifi_blob || persist_authentication_blob) {
            ESP_LOGI(tag, "Migrated local configuration to atomic NVS blobs");
        }
        return true;
    }

    template <std::size_t Size>
    StoredStringStatus load_string(
        const char* const key, std::array<char, Size>& destination
    ) noexcept
    {
        std::size_t required = 0U;
        const auto size_status = nvs_get_str(nvs_handle_, key, nullptr, &required);
        if (size_status == ESP_ERR_NVS_NOT_FOUND) {
            return StoredStringStatus::Missing;
        }
        if (size_status != ESP_OK || required == 0U || required > destination.size()) {
            ESP_LOGW(tag, "Ignoring invalid NVS value for %s", key);
            destination.fill('\0');
            return StoredStringStatus::Invalid;
        }
        if (nvs_get_str(nvs_handle_, key, destination.data(), &required) != ESP_OK) {
            destination.fill('\0');
            return StoredStringStatus::Invalid;
        }
        return StoredStringStatus::Loaded;
    }

    template <typename Value>
    StoredBlobStatus load_blob(
        const char* const key, Value& destination
    ) noexcept
    {
        std::size_t required = 0U;
        const auto size_status = nvs_get_blob(nvs_handle_, key, nullptr, &required);
        if (size_status == ESP_ERR_NVS_NOT_FOUND) {
            return StoredBlobStatus::Missing;
        }
        if (size_status != ESP_OK || required != sizeof(Value)) {
            return StoredBlobStatus::Invalid;
        }
        return nvs_get_blob(nvs_handle_, key, &destination, &required) == ESP_OK
            ? StoredBlobStatus::Loaded
            : StoredBlobStatus::Invalid;
    }

    [[nodiscard]] bool initialize_identity() noexcept
    {
        std::array<std::uint8_t, 6U> mac{};
        const auto status = esp_read_mac(mac.data(), ESP_MAC_WIFI_STA);
        if (status != ESP_OK) {
            ESP_LOGE(tag, "Could not read Wi-Fi MAC: %s", esp_err_to_name(status));
            return false;
        }
        std::snprintf(
            ap_ssid_.data(), ap_ssid_.size(), "Smoker-%02X%02X%02X",
            static_cast<unsigned>(mac[3]),
            static_cast<unsigned>(mac[4]),
            static_cast<unsigned>(mac[5])
        );
        std::snprintf(
            hostname_.data(), hostname_.size(), "smoker-%02x%02x%02x",
            static_cast<unsigned>(mac[3]),
            static_cast<unsigned>(mac[4]),
            static_cast<unsigned>(mac[5])
        );
        return true;
    }

    [[nodiscard]] bool initialize_network_stack() noexcept
    {
        auto status = esp_netif_init();
        if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(tag, "esp_netif init failed: %s", esp_err_to_name(status));
            return false;
        }
        status = esp_event_loop_create_default();
        if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(tag, "event loop init failed: %s", esp_err_to_name(status));
            return false;
        }
        sta_netif_ = esp_netif_create_default_wifi_sta();
        ap_netif_ = esp_netif_create_default_wifi_ap();
        if (sta_netif_ == nullptr || ap_netif_ == nullptr) {
            ESP_LOGE(tag, "Could not create default Wi-Fi netifs");
            return false;
        }
        configure_captive_portal_dhcp();

        wifi_init_config_t wifi_initialization = WIFI_INIT_CONFIG_DEFAULT();
        status = esp_wifi_init(&wifi_initialization);
        if (status != ESP_OK) {
            ESP_LOGE(tag, "Wi-Fi init failed: %s", esp_err_to_name(status));
            return false;
        }
        wifi_started_ = true;
        if (esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID, &Impl::event_handler, this, &wifi_handler_
            ) != ESP_OK
            || esp_event_handler_instance_register(
                IP_EVENT, IP_EVENT_STA_GOT_IP, &Impl::event_handler, this, &ip_handler_
            ) != ESP_OK) {
            ESP_LOGE(tag, "Could not register Wi-Fi event handlers");
            return false;
        }

        const esp_timer_create_args_t timer_arguments{
            &Impl::fallback_timer_callback,
            this,
            ESP_TIMER_TASK,
            "SmokerAPFallback",
            true,
        };
        if (esp_timer_create(&timer_arguments, &fallback_timer_) != ESP_OK) {
            ESP_LOGE(tag, "Could not create SoftAP fallback timer");
            return false;
        }
        const esp_timer_create_args_t sta_only_retry_arguments{
            &Impl::sta_only_retry_callback,
            this,
            ESP_TIMER_TASK,
            "SmokerStaOnly",
            true,
        };
        if (esp_timer_create(
                &sta_only_retry_arguments, &sta_only_retry_timer_
            ) != ESP_OK) {
            ESP_LOGE(tag, "Could not create STA-only transition retry timer");
            return false;
        }
        const esp_timer_create_args_t sta_connect_retry_arguments{
            &Impl::sta_connect_retry_callback,
            this,
            ESP_TIMER_TASK,
            "SmokerStaConnect",
            true,
        };
        if (esp_timer_create(
                &sta_connect_retry_arguments, &sta_connect_retry_timer_
            ) != ESP_OK) {
            ESP_LOGE(tag, "Could not create STA connection retry timer");
            return false;
        }
        const esp_timer_create_args_t scan_timeout_arguments{
            &Impl::scan_timeout_callback,
            this,
            ESP_TIMER_TASK,
            "SmokerWifiScan",
            true,
        };
        if (esp_timer_create(
                &scan_timeout_arguments, &scan_timeout_timer_
            ) != ESP_OK) {
            ESP_LOGE(tag, "Could not create Wi-Fi scan timeout timer");
            return false;
        }

        if (mdns_init() == ESP_OK) {
            mdns_started_ = true;
            static_cast<void>(mdns_hostname_set(hostname_.data()));
            static_cast<void>(mdns_instance_name_set("Smoker local controller"));
            static_cast<void>(mdns_service_add(
                "Smoker local API", "_http", "_tcp", 80U, nullptr, 0U
            ));
        } else {
            ESP_LOGW(tag, "mDNS initialization failed; IP access remains available");
        }
        return true;
    }

    void configure_captive_portal_dhcp() noexcept
    {
        const auto stop_status = esp_netif_dhcps_stop(ap_netif_);
        if (stop_status != ESP_OK && stop_status != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(tag, "Could not pause SoftAP DHCP for option 114: %s", esp_err_to_name(stop_status));
            return;
        }
        const auto option_status = esp_netif_dhcps_option(
            ap_netif_,
            ESP_NETIF_OP_SET,
            ESP_NETIF_CAPTIVEPORTAL_URI,
            const_cast<char*>(captive_portal_uri),
            static_cast<std::uint32_t>(sizeof(captive_portal_uri) - 1U)
        );
        const auto start_status = esp_netif_dhcps_start(ap_netif_);
        if (option_status != ESP_OK || start_status != ESP_OK) {
            ESP_LOGW(
                tag,
                "DHCP captive portal option unavailable (%s / %s); manual IP remains available",
                esp_err_to_name(option_status),
                esp_err_to_name(start_status)
            );
        }
    }

    [[nodiscard]] bool initialize_http() noexcept
    {
        httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
        configuration.core_id = 0;
        configuration.stack_size = 8192U;
        configuration.recv_wait_timeout = 2U;
        configuration.send_wait_timeout = 2U;
        configuration.max_uri_handlers = 24U;
        configuration.global_user_ctx = this;
        if (httpd_start(&http_server_, &configuration) != ESP_OK) {
            ESP_LOGE(tag, "HTTP server start failed");
            return false;
        }

        static constexpr std::array routes{
            std::pair{"/login", HTTP_GET},
            std::pair{"/login", HTTP_POST},
            std::pair{"/login.css", HTTP_GET},
            std::pair{"/login.js", HTTP_GET},
            std::pair{"/", HTTP_GET},
            std::pair{"/app.css", HTTP_GET},
            std::pair{"/app.js", HTTP_GET},
            std::pair{"/setup.css", HTTP_GET},
            std::pair{"/setup.js", HTTP_GET},
            std::pair{"/api/v1/snapshot", HTTP_GET},
            std::pair{"/api/v1/firmware", HTTP_GET},
            std::pair{"/api/v1/firmware/check", HTTP_POST},
            std::pair{"/api/v1/firmware/install", HTTP_POST},
            std::pair{"/api/v1/network", HTTP_GET},
            std::pair{"/api/v1/network", HTTP_PUT},
            std::pair{"/api/v1/network/scan", HTTP_GET},
            std::pair{"/api/v1/network/scan", HTTP_POST},
            std::pair{"/api/v1/setup/status", HTTP_GET},
            std::pair{"/api/v1/setup/network", HTTP_PUT},
            std::pair{"/api/v1/auth/session", HTTP_POST},
            std::pair{"/api/v1/auth/session", HTTP_DELETE},
            std::pair{"/api/v1/auth/password", HTTP_PUT},
            std::pair{"/api/v1/commands", HTTP_POST},
        };
        for (const auto& [uri, method] : routes) {
            httpd_uri_t route{};
            route.uri = uri;
            route.method = method;
            route.handler = &Impl::http_handler;
            route.user_ctx = this;
            if (httpd_register_uri_handler(http_server_, &route) != ESP_OK) {
                ESP_LOGE(tag, "Could not register HTTP route %s", uri);
                return false;
            }
        }
        if (httpd_register_err_handler(
                http_server_, HTTPD_404_NOT_FOUND, &Impl::http_not_found
            ) != ESP_OK) {
            ESP_LOGE(tag, "Could not register captive portal redirect handler");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool start_sta() noexcept
    {
        NetworkConfiguration configuration;
        {
            std::lock_guard lock{mutex_};
            configuration = configuration_;
        }
        wifi_config_t wifi_configuration{};
        std::memcpy(
            wifi_configuration.sta.ssid,
            configuration.ssid.data(),
            std::strlen(configuration.ssid.data())
        );
        std::memcpy(
            wifi_configuration.sta.password,
            configuration.wifi_password.data(),
            std::strlen(configuration.wifi_password.data())
        );
        wifi_configuration.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_configuration.sta.pmf_cfg.capable = true;
        wifi_configuration.sta.pmf_cfg.required = false;

        std::lock_guard mode_lock{wifi_mode_mutex_};
        auto status = esp_wifi_set_mode(WIFI_MODE_STA);
        if (status == ESP_OK) {
            status = esp_wifi_set_config(WIFI_IF_STA, &wifi_configuration);
        }
        if (status == ESP_OK) {
            status = esp_wifi_start();
        }
        if (status != ESP_OK) {
            ESP_LOGE(tag, "Could not start STA mode: %s", esp_err_to_name(status));
            return false;
        }
        return true;
    }

    [[nodiscard]] esp_err_t set_wifi_mode_serialized(
        const wifi_mode_t mode
    ) noexcept
    {
        std::lock_guard mode_lock{wifi_mode_mutex_};
        return esp_wifi_set_mode(mode);
    }

    [[nodiscard]] bool enable_soft_ap(
        const bool keep_sta, const bool require_disconnected = false
    ) noexcept
    {
        std::lock_guard mode_lock{wifi_mode_mutex_};
        return enable_soft_ap_locked(keep_sta, require_disconnected);
    }

    [[nodiscard]] bool enable_soft_ap_locked(
        const bool keep_sta, const bool require_disconnected
    ) noexcept
    {
        if (require_disconnected) {
            std::lock_guard lock{mutex_};
            if (!fallback_coordinator_.permit_enable(status_.sta_connected)) {
                return true;
            }
        }
        wifi_config_t access_point{};
        std::memcpy(
            access_point.ap.ssid, ap_ssid_.data(), std::strlen(ap_ssid_.data())
        );
        access_point.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(ap_ssid_.data()));
        access_point.ap.channel = 1U;
        access_point.ap.authmode = WIFI_AUTH_OPEN;
        access_point.ap.max_connection = 4U;
        access_point.ap.pmf_cfg.capable = true;
        access_point.ap.pmf_cfg.required = false;

        const auto mode = keep_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP;
        if (esp_wifi_set_mode(mode) != ESP_OK
            || esp_wifi_set_config(WIFI_IF_AP, &access_point) != ESP_OK
            || (!wifi_running_.load(std::memory_order_acquire)
                && esp_wifi_start() != ESP_OK)) {
            ESP_LOGE(tag, "Could not enable SoftAP fallback");
            return false;
        }
        wifi_running_.store(true, std::memory_order_release);
        {
            std::lock_guard lock{mutex_};
            status_.ap_active = true;
        }
        ESP_LOGW(
            tag,
            "Open commissioning-only SoftAP %s active at 192.168.4.1",
            ap_ssid_.data()
        );
        return true;
    }

    void start_dns_responder() noexcept
    {
        reap_exited_dns_task();
        if (dns_task_.load(std::memory_order_acquire) != nullptr) {
            ESP_LOGW(tag, "Previous captive DNS task has not exited; manual IP remains available");
            return;
        }
        if (dns_running_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        dns_task_exited_.store(false, std::memory_order_release);
        static_assert(
            dns_stack_bytes % sizeof(StackType_t) == 0U,
            "DNS task stack must contain whole StackType_t entries"
        );
        auto* const task = xTaskCreateStaticPinnedToCore(
            &Impl::dns_task_entry,
            "SmokerDns",
            static_cast<std::uint32_t>(dns_stack_.size()),
            this,
            1U,
            dns_stack_.data(),
            &dns_task_storage_,
            0
        );
        if (task == nullptr) {
            dns_running_.store(false, std::memory_order_release);
            ESP_LOGW(tag, "Captive DNS task unavailable; manual 192.168.4.1 access remains available");
            return;
        }
        dns_task_.store(task, std::memory_order_release);
    }

    void stop_dns_responder() noexcept
    {
        dns_running_.store(false, std::memory_order_release);
        const int socket = dns_socket_.exchange(-1, std::memory_order_acq_rel);
        if (socket >= 0) {
            static_cast<void>(shutdown(socket, SHUT_RDWR));
            static_cast<void>(close(socket));
        }
        for (std::size_t attempt = 0U;
             attempt < 400U && !dns_task_exited_.load(std::memory_order_acquire)
                 && dns_task_.load(std::memory_order_acquire) != nullptr;
             ++attempt) {
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
        reap_exited_dns_task();
        if (dns_task_.load(std::memory_order_acquire) != nullptr) {
            ESP_LOGW(tag, "Captive DNS stop timed out; static storage will not be reused");
        }
    }

    void reap_exited_dns_task() noexcept
    {
        if (!dns_task_exited_.load(std::memory_order_acquire)) {
            return;
        }
        auto* const task = dns_task_.exchange(nullptr, std::memory_order_acq_rel);
        if (task != nullptr) {
            vTaskDelete(task);
        }
        dns_task_exited_.store(false, std::memory_order_release);
    }

    [[noreturn]] void finish_dns_task() noexcept
    {
        dns_running_.store(false, std::memory_order_release);
        dns_task_exited_.store(true, std::memory_order_release);
        while (true) {
            vTaskSuspend(nullptr);
        }
    }

    static void dns_task_entry(void* const context) noexcept
    {
        static_cast<Impl*>(context)->run_dns_responder();
    }

    void run_dns_responder() noexcept
    {
        esp_netif_ip_info_t ip_info{};
        if (esp_netif_get_ip_info(ap_netif_, &ip_info) != ESP_OK) {
            ESP_LOGW(tag, "Captive DNS could not read the SoftAP address");
            finish_dns_task();
        }

        const int socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (socket < 0) {
            ESP_LOGW(tag, "Captive DNS socket creation failed: errno %d", errno);
            finish_dns_task();
        }
        dns_socket_.store(socket, std::memory_order_release);
        const timeval timeout{0, 250000};
        static_cast<void>(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(53U);
        local.sin_addr.s_addr = ip_info.ip.addr;
        if (bind(socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            ESP_LOGW(tag, "Captive DNS bind failed: errno %d", errno);
            const int owned = dns_socket_.exchange(-1, std::memory_order_acq_rel);
            if (owned >= 0) {
                static_cast<void>(close(owned));
            }
            finish_dns_task();
        }
        ESP_LOGI(tag, "Captive DNS active on SoftAP core 0");

        constexpr std::array<std::uint8_t, 4U> portal_address{192U, 168U, 4U, 1U};
        while (dns_running_.load(std::memory_order_acquire)) {
            sockaddr_storage source{};
            socklen_t source_length = sizeof(source);
            const auto received = recvfrom(
                socket,
                dns_request_.data(),
                dns_request_.size(),
                0,
                reinterpret_cast<sockaddr*>(&source),
                &source_length
            );
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;
            }
            const auto result = build_captive_dns_response(
                std::span<const std::uint8_t>{
                    dns_request_.data(), static_cast<std::size_t>(received)
                },
                dns_response_,
                portal_address
            );
            if ((result.status == DnsResponseStatus::Answer
                 || result.status == DnsResponseStatus::NoAnswer)
                && result.length > 0U) {
                static_cast<void>(sendto(
                    socket,
                    dns_response_.data(),
                    result.length,
                    0,
                    reinterpret_cast<const sockaddr*>(&source),
                    source_length
                ));
            }
        }

        const int owned = dns_socket_.exchange(-1, std::memory_order_acq_rel);
        if (owned >= 0) {
            static_cast<void>(close(owned));
        }
        finish_dns_task();
    }

    void schedule_fallback() noexcept
    {
        if (fallback_timer_ == nullptr) {
            return;
        }
        esp_err_t status = ESP_OK;
        {
            std::lock_guard lock{mutex_};
            if (!fallback_coordinator_.arm(
                    status_.ap_active, status_.sta_connected
                )) {
                return;
            }
            status = esp_timer_start_once(
                fallback_timer_, fallback_delay_microseconds
            );
            if (status != ESP_OK) {
                fallback_coordinator_.cancel();
            }
        }
        if (status != ESP_OK) {
            ESP_LOGW(tag, "Could not schedule SoftAP fallback: %s", esp_err_to_name(status));
        }
    }

    [[nodiscard]] bool request_network_scan() noexcept
    {
        bool sta_configured = false;
        bool sta_connected = false;
        {
            std::lock_guard lock{mutex_};
            sta_configured = configuration_.has_sta_credentials;
            sta_connected = status_.sta_connected;
            const auto action = scan_coordinator_.request(sta_configured, sta_connected);
            if (!action.start_scan) {
                return true;
            }
            scan_driver_active_ = false;
        }

        if (scan_timeout_timer_ == nullptr
            || esp_timer_start_once(
                   scan_timeout_timer_, scan_timeout_microseconds
               ) != ESP_OK) {
            ESP_LOGW(tag, "Could not arm bounded Wi-Fi scan timeout");
            fail_network_scan();
            return false;
        }

        wifi_mode_t mode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&mode) != ESP_OK) {
            fail_network_scan();
            return false;
        }
        if (mode == WIFI_MODE_AP) {
            if (set_wifi_mode_serialized(WIFI_MODE_APSTA) != ESP_OK) {
                fail_network_scan();
                return false;
            }
        }

        if (sta_configured && !sta_connected) {
            const auto disconnect_status = esp_wifi_disconnect();
            if (disconnect_status == ESP_OK) {
                return false;
            }
            if (disconnect_status != ESP_ERR_WIFI_NOT_CONNECT) {
                ESP_LOGW(
                    tag,
                    "Could not serialize STA before scan: %s",
                    esp_err_to_name(disconnect_status)
                );
            }
        }
        try_start_network_scan();
        return false;
    }

    void try_start_network_scan() noexcept
    {
        {
            std::lock_guard lock{mutex_};
            if (!scan_coordinator_.blocks_sta_reconnect() || scan_driver_active_) {
                return;
            }
            scan_driver_active_ = true;
        }

        wifi_scan_config_t scan_configuration{};
        scan_configuration.show_hidden = false;
        scan_configuration.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        const auto scan_status = esp_wifi_scan_start(&scan_configuration, false);
        if (scan_status != ESP_OK) {
            ESP_LOGW(tag, "Wi-Fi scan start failed: %s", esp_err_to_name(scan_status));
            fail_network_scan();
        }
    }

    void fail_network_scan() noexcept
    {
        bool reconnect = false;
        bool apply_pending = false;
        {
            std::lock_guard lock{mutex_};
            if (!scan_coordinator_.blocks_sta_reconnect()) {
                return;
            }
            scan_driver_active_ = false;
            reconnect = scan_coordinator_.fail(status_.sta_connected).reconnect_sta;
            apply_pending = wifi_apply_after_scan_;
            wifi_apply_after_scan_ = false;
        }
        if (scan_timeout_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(scan_timeout_timer_));
        }
        return_to_ap_if_unconfigured();
        if (apply_pending) {
            static_cast<void>(apply_updated_wifi_configuration());
        } else if (reconnect) {
            connect_sta_or_retry();
            schedule_fallback();
        }
    }

    void finish_network_scan(const wifi_event_sta_scan_done_t* const event) noexcept
    {
        {
            std::lock_guard lock{mutex_};
            if (!scan_coordinator_.blocks_sta_reconnect()) {
                static_cast<void>(esp_wifi_clear_ap_list());
                return;
            }
        }
        if (scan_timeout_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(scan_timeout_timer_));
        }
        if (event == nullptr || event->status != 0U) {
            static_cast<void>(esp_wifi_clear_ap_list());
            fail_network_scan();
            return;
        }

        std::uint16_t discovered = 0U;
        if (esp_wifi_scan_get_ap_num(&discovered) != ESP_OK) {
            static_cast<void>(esp_wifi_clear_ap_list());
            fail_network_scan();
            return;
        }
        std::uint16_t received = std::min<std::uint16_t>(
            discovered, static_cast<std::uint16_t>(wifi_scan_records_.size())
        );
        if (received > 0U
            && esp_wifi_scan_get_ap_records(&received, wifi_scan_records_.data()) != ESP_OK) {
            static_cast<void>(esp_wifi_clear_ap_list());
            fail_network_scan();
            return;
        }
        if (received == 0U) {
            static_cast<void>(esp_wifi_clear_ap_list());
        }

        for (std::size_t index = 0U; index < static_cast<std::size_t>(received); ++index) {
            const auto& source = wifi_scan_records_[index];
            auto& destination = raw_scan_records_[index];
            destination = {};
            while (destination.ssid_length < destination.ssid.size()
                   && source.ssid[destination.ssid_length] != 0U) {
                destination.ssid[destination.ssid_length] =
                    source.ssid[destination.ssid_length];
                ++destination.ssid_length;
            }
            destination.rssi_dbm = source.rssi;
            destination.channel = source.primary;
            destination.security = security_category(source.authmode);
        }
        auto curated = curate_wifi_networks(std::span<const RawWifiNetwork>{
            raw_scan_records_.data(), static_cast<std::size_t>(received)
        });
        curated.truncated = curated.truncated
            || discovered > static_cast<std::uint16_t>(wifi_scan_records_.size());

        bool reconnect = false;
        bool apply_pending = false;
        {
            std::lock_guard lock{mutex_};
            scan_results_ = curated;
            scan_driver_active_ = false;
            reconnect = scan_coordinator_.complete(status_.sta_connected).reconnect_sta;
            apply_pending = wifi_apply_after_scan_;
            wifi_apply_after_scan_ = false;
        }
        return_to_ap_if_unconfigured();
        if (apply_pending) {
            static_cast<void>(apply_updated_wifi_configuration());
        } else if (reconnect) {
            connect_sta_or_retry();
            schedule_fallback();
        }
    }

    void return_to_ap_if_unconfigured() noexcept
    {
        // Keep the decision and driver transition inside the same mode
        // serialization boundary. Otherwise a concurrent provisioning PUT can
        // apply APSTA/connect and then be overwritten by a stale scan callback.
        std::lock_guard mode_lock{wifi_mode_mutex_};
        {
            std::lock_guard lock{mutex_};
            if (!status_.ap_active || configuration_.has_sta_credentials) {
                return;
            }
        }
        const auto status = esp_wifi_set_mode(WIFI_MODE_AP);
        if (status != ESP_OK) {
            ESP_LOGW(
                tag,
                "Could not restore AP-only mode after scan: %s",
                esp_err_to_name(status)
            );
        }
    }

    static void fallback_timer_callback(void* const context) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        bool enable_ap = false;
        {
            std::lock_guard lock{self->mutex_};
            enable_ap = self->fallback_coordinator_.expire(
                self->status_.sta_connected
            );
        }
        if (enable_ap) {
            if (!self->enable_soft_ap(true, true)) {
                self->schedule_fallback();
            }
        }
    }

    static void scan_timeout_callback(void* const context) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        ESP_LOGW(tag, "Wi-Fi scan timed out; restoring reconnect/provisioning flow");
        const auto status = esp_wifi_scan_stop();
        if (status != ESP_OK && status != ESP_ERR_WIFI_STATE) {
            ESP_LOGW(tag, "Wi-Fi scan stop after timeout failed: %s", esp_err_to_name(status));
        }
        self->fail_network_scan();
    }

    void schedule_sta_only_retry() noexcept
    {
        if (sta_only_retry_timer_ == nullptr
            || esp_timer_is_active(sta_only_retry_timer_)) {
            return;
        }
        const auto status = esp_timer_start_once(
            sta_only_retry_timer_, sta_only_retry_microseconds
        );
        if (status != ESP_OK) {
            ESP_LOGE(
                tag,
                "Could not schedule STA-only transition retry: %s",
                esp_err_to_name(status)
            );
        }
    }

    void schedule_sta_connect_retry() noexcept
    {
        if (sta_connect_retry_timer_ == nullptr
            || esp_timer_is_active(sta_connect_retry_timer_)) {
            return;
        }
        const auto status = esp_timer_start_once(
            sta_connect_retry_timer_, sta_connect_retry_microseconds
        );
        if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(tag, "Could not schedule STA reconnect retry: %s", esp_err_to_name(status));
        }
    }

    void connect_sta_or_retry() noexcept
    {
        std::lock_guard mode_lock{wifi_mode_mutex_};
        {
            std::lock_guard lock{mutex_};
            if (!configuration_.has_sta_credentials || status_.sta_connected
                || scan_coordinator_.blocks_sta_reconnect()
                || sta_configuration_transition_ || sta_connect_in_flight_) {
                return;
            }
        }
        wifi_mode_t mode = WIFI_MODE_NULL;
        const auto mode_status = esp_wifi_get_mode(&mode);
        if (mode_status != ESP_OK) {
            ESP_LOGW(tag, "Could not inspect mode before STA reconnect: %s", esp_err_to_name(mode_status));
            schedule_sta_connect_retry();
            return;
        }
        if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
            return;
        }
        {
            std::lock_guard lock{mutex_};
            if (!configuration_.has_sta_credentials || status_.sta_connected
                || scan_coordinator_.blocks_sta_reconnect()
                || sta_configuration_transition_ || sta_connect_in_flight_) {
                return;
            }
            sta_connect_in_flight_ = true;
        }
        const auto status = esp_wifi_connect();
        if (status != ESP_OK) {
            {
                std::lock_guard lock{mutex_};
                sta_connect_in_flight_ = false;
            }
            ESP_LOGW(tag, "STA reconnect request failed: %s; retrying", esp_err_to_name(status));
            schedule_sta_connect_retry();
        } else if (sta_connect_retry_timer_ != nullptr) {
            static_cast<void>(esp_timer_stop(sta_connect_retry_timer_));
        }
    }

    [[nodiscard]] bool transition_to_sta_only() noexcept
    {
        std::lock_guard mode_lock{wifi_mode_mutex_};
        std::array<char, 16U> sta_ip{};
        {
            std::lock_guard lock{mutex_};
            if (!status_.sta_connected || sta_configuration_transition_) {
                return false;
            }
            sta_ip = status_.sta_ip;
        }
        const auto mode_status = esp_wifi_set_mode(WIFI_MODE_STA);
        if (mode_status != ESP_OK) {
            ESP_LOGE(
                tag,
                "STA got IP at %s but SoftAP disable failed: %s; retrying",
                sta_ip.data(),
                esp_err_to_name(mode_status)
            );
            schedule_sta_only_retry();
            return false;
        }
        if (sta_only_retry_timer_ != nullptr
            && esp_timer_is_active(sta_only_retry_timer_)) {
            static_cast<void>(esp_timer_stop(sta_only_retry_timer_));
        }
        ESP_LOGI(
            tag,
            "STA connected at %s; SoftAP disabled; http://%s.local/",
            sta_ip.data(),
            hostname_.data()
        );
        return true;
    }

    static void sta_only_retry_callback(void* const context) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        static_cast<void>(self->transition_to_sta_only());
    }

    static void sta_connect_retry_callback(void* const context) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        WifiStaRetryAction action = WifiStaRetryAction::Connect;
        {
            std::lock_guard lock{self->mutex_};
            action = self->sta_retry_state_.action();
        }
        if (action == WifiStaRetryAction::ReapplyConfiguration) {
            static_cast<void>(self->apply_updated_wifi_configuration());
        } else {
            self->connect_sta_or_retry();
        }
    }

    static void event_handler(
        void* const context,
        esp_event_base_t event_base,
        const std::int32_t event_id,
        void* const event_data
    ) noexcept
    {
        auto* const self = static_cast<Impl*>(context);
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            self->wifi_running_.store(true, std::memory_order_release);
            bool scan_blocks_reconnect = false;
            bool sta_configured = false;
            bool configuration_transition = false;
            {
                std::lock_guard lock{self->mutex_};
                scan_blocks_reconnect = self->scan_coordinator_.blocks_sta_reconnect();
                sta_configured = self->configuration_.has_sta_credentials;
                configuration_transition = self->sta_configuration_transition_;
            }
            if (scan_blocks_reconnect) {
                self->try_start_network_scan();
            } else if (sta_configured && !configuration_transition) {
                self->connect_sta_or_retry();
            }
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto* const disconnected = static_cast<const wifi_event_sta_disconnected_t*>(
                event_data
            );
            bool scan_blocks_reconnect = false;
            bool sta_configured = false;
            bool configuration_transition = false;
            bool configuration_disconnect = false;
            {
                std::lock_guard mode_lock{self->wifi_mode_mutex_};
                {
                    std::lock_guard lock{self->mutex_};
                    self->status_.sta_connected = false;
                    self->status_.sta_ipv4 = 0U;
                    self->status_.sta_ip.fill('\0');
                    configuration_disconnect = self->configuration_disconnect_pending_
                        && disconnected != nullptr
                        && disconnected->reason == WIFI_REASON_ASSOC_LEAVE;
                    self->configuration_disconnect_pending_ = false;
                    if (configuration_disconnect) {
                        self->status_.sta_last_error.fill('\0');
                    } else if (disconnected != nullptr) {
                        static_cast<void>(copy_bounded(
                            self->status_.sta_last_error,
                            sta_disconnect_error(disconnected->reason)
                        ));
                    }
                    if (!configuration_disconnect) {
                        self->sta_connect_in_flight_ = false;
                    }
                    sta_configured = self->configuration_.has_sta_credentials;
                    configuration_transition = self->sta_configuration_transition_;
                    self->scan_coordinator_.note_sta_disconnected(sta_configured);
                    scan_blocks_reconnect = self->scan_coordinator_.blocks_sta_reconnect();
                }
            }
            if (scan_blocks_reconnect) {
                self->try_start_network_scan();
            } else if (sta_configured && !configuration_transition) {
                self->connect_sta_or_retry();
            }
            self->schedule_fallback();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
            self->finish_network_scan(
                static_cast<const wifi_event_sta_scan_done_t*>(event_data)
            );
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
            {
                std::lock_guard lock{self->mutex_};
                self->status_.ap_active = true;
            }
            if (!self->transition_to_sta_only()) {
                self->start_dns_responder();
                self->connect_sta_or_retry();
            }
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP) {
            bool needs_fallback = false;
            {
                std::lock_guard lock{self->mutex_};
                self->status_.ap_active = false;
                needs_fallback = self->configuration_.has_sta_credentials
                    && !self->status_.sta_connected;
            }
            self->stop_dns_responder();
            if (needs_fallback) {
                self->schedule_fallback();
            }
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            const auto* const event = static_cast<const ip_event_got_ip_t*>(event_data);
            std::lock_guard mode_lock{self->wifi_mode_mutex_};
            NetworkConfiguration expected_configuration;
            {
                std::lock_guard lock{self->mutex_};
                if (self->sta_configuration_transition_) {
                    ESP_LOGW(tag, "Ignoring stale GOT_IP during STA configuration transition");
                    return;
                }
                expected_configuration = self->configuration_;
            }
            wifi_ap_record_t associated_access_point{};
            if (esp_wifi_sta_get_ap_info(&associated_access_point) != ESP_OK
                || std::strncmp(
                       reinterpret_cast<const char*>(associated_access_point.ssid),
                       expected_configuration.ssid.data(),
                       expected_configuration.ssid.size() - 1U
                   ) != 0) {
                ESP_LOGW(tag, "Ignoring GOT_IP that does not match the active STA configuration");
                return;
            }
            {
                std::lock_guard lock{self->mutex_};
                self->status_.sta_connected = true;
                self->status_.sta_ipv4 = ntohl(event->ip_info.ip.addr);
                self->status_.sta_last_error.fill('\0');
                self->sta_connect_in_flight_ = false;
                self->configuration_disconnect_pending_ = false;
                self->fallback_coordinator_.cancel();
                std::snprintf(
                    self->status_.sta_ip.data(),
                    self->status_.sta_ip.size(),
                    IPSTR,
                    IP2STR(&event->ip_info.ip)
                );
            }
            static_cast<void>(esp_timer_stop(self->fallback_timer_));
            static_cast<void>(esp_timer_stop(self->sta_connect_retry_timer_));
            const auto mode_status = esp_wifi_set_mode(WIFI_MODE_STA);
            if (mode_status != ESP_OK) {
                ESP_LOGE(
                    tag,
                    "STA got IP but SoftAP disable failed: %s; retrying",
                    esp_err_to_name(mode_status)
                );
                self->schedule_sta_only_retry();
            } else {
                if (self->sta_only_retry_timer_ != nullptr) {
                    static_cast<void>(esp_timer_stop(self->sta_only_retry_timer_));
                }
                ESP_LOGI(
                    tag,
                    "STA connected at " IPSTR "; SoftAP disabled; http://%s.local/",
                    IP2STR(&event->ip_info.ip),
                    self->hostname_.data()
                );
            }
        }
    }

    static esp_err_t http_handler(httpd_req_t* const request)
    {
        auto* const self = static_cast<Impl*>(httpd_get_global_user_ctx(request->handle));
        if (self == nullptr) {
            return ESP_FAIL;
        }
        const auto scope = self->request_scope(request);
        if (scope == HttpRequestScope::Rejected) {
            return self->send_error(
                request, "421 Misdirected Request", "interfață locală respinsă"
            );
        }
        if (!self->request_host_allowed(request, scope)) {
            if (scope == HttpRequestScope::Commissioning
                && request->method == HTTP_GET
                && std::string_view{request->uri}.find("/api/") != 0U) {
                return self->send_setup_redirect(request);
            }
            return self->send_error(
                request, "421 Misdirected Request", "gazdă HTTP respinsă"
            );
        }

        if (scope == HttpRequestScope::Commissioning) {
            if (!self->same_origin(request, scope)) {
                return self->send_error(request, "403 Forbidden", "origine respinsă");
            }
            if (std::strcmp(request->uri, "/") == 0) {
                return self->send_asset(
                    request, "text/html; charset=utf-8", web_assets::setup_html
                );
            }
            if (std::strcmp(request->uri, "/setup.css") == 0) {
                return self->send_asset(
                    request, "text/css; charset=utf-8", web_assets::setup_css
                );
            }
            if (std::strcmp(request->uri, "/setup.js") == 0) {
                return self->send_asset(
                    request, "application/javascript; charset=utf-8", web_assets::setup_js
                );
            }
            if (std::strcmp(request->uri, "/api/v1/setup/status") == 0) {
                return self->handle_setup_status(request);
            }
            if (std::strcmp(request->uri, "/api/v1/network/scan") == 0
                && request->method == HTTP_GET) {
                return self->handle_scan_get(request);
            }
            if (std::strcmp(request->uri, "/api/v1/network/scan") == 0
                && request->method == HTTP_POST) {
                return self->handle_scan_post(request);
            }
            if (std::strcmp(request->uri, "/api/v1/setup/network") == 0
                && request->method == HTTP_PUT) {
                return self->handle_network_put(request);
            }
            return self->send_error(
                request, "403 Forbidden", "ruta nu este disponibilă prin SoftAP"
            );
        }

        if (std::strcmp(request->uri, "/login") == 0) {
            return request->method == HTTP_GET
                ? self->send_login_page(request, false)
                : self->handle_login_post(request);
        }
        if (std::strcmp(request->uri, "/login.css") == 0) {
            return self->send_asset(
                request, "text/css; charset=utf-8", web_assets::login_css
            );
        }
        if (std::strcmp(request->uri, "/login.js") == 0) {
            return self->send_asset(
                request, "application/javascript; charset=utf-8", web_assets::login_js
            );
        }
        if (std::strcmp(request->uri, "/api/v1/auth/session") == 0
            && request->method == HTTP_POST) {
            if (!self->same_origin(request, scope)) {
                return self->send_error(request, "403 Forbidden", "origine respinsă");
            }
            return self->handle_auth_session_post(request);
        }
        if (!self->authenticate(request)) {
            return ESP_OK;
        }
        if (!self->same_origin(request, scope)) {
            return self->send_error(request, "403 Forbidden", "origine browser respinsă");
        }
        if (std::strcmp(request->uri, "/api/v1/auth/session") == 0
            && request->method == HTTP_DELETE) {
            return self->handle_logout(request);
        }
        if (std::strcmp(request->uri, "/api/v1/auth/password") == 0
            && request->method == HTTP_PUT) {
            return self->handle_password_put(request);
        }
        if (std::strcmp(request->uri, "/") == 0) {
            return self->send_asset(request, "text/html; charset=utf-8", web_assets::index_html);
        }
        if (std::strcmp(request->uri, "/app.css") == 0) {
            return self->send_asset(request, "text/css; charset=utf-8", web_assets::app_css);
        }
        if (std::strcmp(request->uri, "/app.js") == 0) {
            return self->send_asset(
                request, "application/javascript; charset=utf-8", web_assets::app_js
            );
        }
        if (std::strcmp(request->uri, "/api/v1/snapshot") == 0) {
            return self->handle_snapshot(request);
        }
        if (std::strcmp(request->uri, "/api/v1/firmware") == 0) {
            return self->handle_firmware_get(request);
        }
        if (std::strcmp(request->uri, "/api/v1/firmware/check") == 0) {
            return self->handle_firmware_check(request);
        }
        if (std::strcmp(request->uri, "/api/v1/firmware/install") == 0) {
            return self->handle_firmware_install(request);
        }
        if (std::strcmp(request->uri, "/api/v1/network") == 0
            && request->method == HTTP_GET) {
            return self->handle_network_get(request);
        }
        if (std::strcmp(request->uri, "/api/v1/network") == 0
            && request->method == HTTP_PUT) {
            return self->handle_network_put(request);
        }
        if (std::strcmp(request->uri, "/api/v1/network/scan") == 0
            && request->method == HTTP_GET) {
            return self->handle_scan_get(request);
        }
        if (std::strcmp(request->uri, "/api/v1/network/scan") == 0
            && request->method == HTTP_POST) {
            return self->handle_scan_post(request);
        }
        if (std::strcmp(request->uri, "/api/v1/commands") == 0) {
            return self->handle_command(request);
        }
        return self->send_error(request, "404 Not Found", "rută necunoscută");
    }

    static esp_err_t http_not_found(
        httpd_req_t* const request, const httpd_err_code_t
    ) noexcept
    {
        auto* const self = static_cast<Impl*>(httpd_get_global_user_ctx(request->handle));
        if (self == nullptr) {
            return ESP_FAIL;
        }
        const auto scope = self->request_scope(request);
        if (scope == HttpRequestScope::Rejected) {
            return self->send_error(
                request, "421 Misdirected Request", "interfață locală respinsă"
            );
        }
        if (scope == HttpRequestScope::Commissioning) {
            return self->send_setup_redirect(request);
        }
        return self->send_login_redirect(request);
    }

    void add_response_headers(httpd_req_t* const request) const noexcept
    {
        static_cast<void>(httpd_resp_set_hdr(request, "Cache-Control", "no-store"));
        static_cast<void>(httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff"));
        static_cast<void>(httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer"));
        static_cast<void>(httpd_resp_set_hdr(request, "X-Frame-Options", "DENY"));
        static_cast<void>(httpd_resp_set_hdr(
            request,
            "Permissions-Policy",
            "camera=(), microphone=(), geolocation=()"
        ));
        static_cast<void>(httpd_resp_set_hdr(
            request,
            "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; "
            "img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; "
            "form-action 'self'"
        ));
    }

    [[nodiscard]] bool soft_ap_active() const noexcept
    {
        std::lock_guard lock{mutex_};
        return status_.ap_active;
    }

    [[nodiscard]] bool request_host_allowed(
        httpd_req_t* const request, const HttpRequestScope scope
    ) const noexcept
    {
        const auto host_length = httpd_req_get_hdr_value_len(request, "Host");
        if (host_length == 0U || host_length >= 160U) {
            return false;
        }
        std::array<char, 160U> host{};
        if (httpd_req_get_hdr_value_str(
                request, "Host", host.data(), host.size()
            ) != ESP_OK) {
            return false;
        }
        NetworkStatus network_status;
        {
            std::lock_guard lock{mutex_};
            network_status = status_;
        }
        std::array<char, 32U> local_hostname{};
        std::snprintf(
            local_hostname.data(), local_hostname.size(), "%s.local", hostname_.data()
        );
        const std::string_view value{host.data()};
        if (scope == HttpRequestScope::Commissioning) {
            return network_status.ap_active
                && host_matches_device_authority(value, "192.168.4.1");
        }
        return scope == HttpRequestScope::Operational
            && network_status.sta_connected
            && (host_matches_device_authority(value, network_status.sta_ip.data())
                || host_matches_device_authority(value, local_hostname.data()));
    }

    [[nodiscard]] HttpRequestScope request_scope(
        httpd_req_t* const request
    ) const noexcept
    {
        const int socket = httpd_req_to_sockfd(request);
        sockaddr_storage local{};
        socklen_t length = sizeof(local);
        if (socket < 0
            || getsockname(socket, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
            return HttpRequestScope::Rejected;
        }
        std::optional<std::uint32_t> local_ipv4;
        if (local.ss_family == AF_INET) {
            const auto* const ipv4 = reinterpret_cast<const sockaddr_in*>(&local);
            local_ipv4 = ntohl(ipv4->sin_addr.s_addr);
        } else if (local.ss_family == AF_INET6) {
            const auto* const ipv6 = reinterpret_cast<const sockaddr_in6*>(&local);
            local_ipv4 = extract_ipv4_mapped_address(
                std::span<const std::uint8_t, 16U>{ipv6->sin6_addr.s6_addr, 16U}
            );
        }
        if (!local_ipv4) {
            return HttpRequestScope::Rejected;
        }
        NetworkStatus network_status;
        {
            std::lock_guard lock{mutex_};
            network_status = status_;
        }
        const auto sta_ipv4 = network_status.sta_connected
                && network_status.sta_ipv4 != 0U
            ? std::optional<std::uint32_t>{network_status.sta_ipv4}
            : std::nullopt;
        return classify_http_request_scope(
            *local_ipv4, sta_ipv4, network_status.ap_active
        );
    }

    esp_err_t send_login_redirect(httpd_req_t* const request) const noexcept
    {
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "303 See Other"));
        static_cast<void>(httpd_resp_set_hdr(request, "Location", "/login"));
        static_cast<void>(httpd_resp_set_type(request, "text/plain; charset=utf-8"));
        constexpr std::string_view body = "Deschide portalul local Fumuri.";
        return httpd_resp_send(
            request, body.data(), static_cast<ssize_t>(body.size())
        );
    }

    esp_err_t send_setup_redirect(httpd_req_t* const request) const noexcept
    {
        add_response_headers(request);
        // Captive-network assistants (notably iOS) expect the conventional
        // temporary captive-portal redirect for their connectivity probe.
        static_cast<void>(httpd_resp_set_status(request, "302 Found"));
        static_cast<void>(httpd_resp_set_hdr(request, "Location", captive_portal_uri));
        static_cast<void>(httpd_resp_set_type(request, "text/plain; charset=utf-8"));
        constexpr std::string_view body = "Deschide configurarea Wi-Fi Fumuri.";
        return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
    }

    esp_err_t send_login_page(
        httpd_req_t* const request, const bool credentials_invalid
    ) const noexcept
    {
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(
            request, credentials_invalid ? "401 Unauthorized" : "200 OK"
        ));
        static_cast<void>(httpd_resp_set_type(request, "text/html; charset=utf-8"));
        const auto content = credentials_invalid
            ? web_assets::login_error_html
            : web_assets::login_html;
        return httpd_resp_send(
            request, content.data(), static_cast<ssize_t>(content.size())
        );
    }

    esp_err_t send_login_rate_limited(
        httpd_req_t* const request, const std::uint32_t retry_after_seconds
    ) const noexcept
    {
        std::array<char, 16U> retry_after{};
        std::snprintf(
            retry_after.data(), retry_after.size(), "%lu",
            static_cast<unsigned long>(retry_after_seconds)
        );
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "429 Too Many Requests"));
        static_cast<void>(httpd_resp_set_hdr(
            request, "Retry-After", retry_after.data()
        ));
        static_cast<void>(httpd_resp_set_type(request, "text/html; charset=utf-8"));
        return httpd_resp_send(
            request,
            web_assets::login_rate_limited_html.data(),
            static_cast<ssize_t>(web_assets::login_rate_limited_html.size())
        );
    }

    esp_err_t send_api_rate_limited(
        httpd_req_t* const request, const std::uint32_t retry_after_seconds
    ) const noexcept
    {
        std::array<char, 16U> retry_after{};
        std::snprintf(
            retry_after.data(), retry_after.size(), "%lu",
            static_cast<unsigned long>(retry_after_seconds)
        );
        static_cast<void>(httpd_resp_set_hdr(
            request, "Retry-After", retry_after.data()
        ));
        return send_error(
            request, "429 Too Many Requests", "autentificare limitată temporar"
        );
    }

    bool read_login_form(
        httpd_req_t* const request,
        std::array<char, 64U>& password
    ) const noexcept
    {
        if (request->content_len <= 0
            || request->content_len > static_cast<int>(maximum_body_bytes)) {
            return false;
        }
        std::array<char, 64U> content_type{};
        if (httpd_req_get_hdr_value_str(
                request, "Content-Type", content_type.data(), content_type.size()
            ) != ESP_OK
            || !http_content_type_matches(
                content_type.data(), "application/x-www-form-urlencoded"
            )) {
            return false;
        }

        std::array<char, maximum_body_bytes + 1U> body{};
        int received_total = 0;
        std::size_t receive_timeouts = 0U;
        const auto receive_deadline =
            esp_timer_get_time() + body_receive_deadline_microseconds;
        while (received_total < request->content_len) {
            if (esp_timer_get_time() >= receive_deadline) {
                return false;
            }
            const auto received = httpd_req_recv(
                request,
                body.data() + received_total,
                static_cast<std::size_t>(request->content_len)
                    - static_cast<std::size_t>(received_total)
            );
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                if (receive_timeouts++ >= maximum_body_receive_timeouts) {
                    return false;
                }
                continue;
            }
            if (received <= 0) {
                return false;
            }
            received_total += received;
            if (esp_timer_get_time() >= receive_deadline
                && received_total < request->content_len) {
                return false;
            }
        }

        const std::string_view form{body.data(), static_cast<std::size_t>(received_total)};
        bool has_password = false;
        std::size_t field_start = 0U;
        while (field_start <= form.size()) {
            const auto field_end = form.find('&', field_start);
            const auto field = form.substr(
                field_start,
                field_end == std::string_view::npos
                    ? form.size() - field_start
                    : field_end - field_start
            );
            const auto separator = field.find('=');
            if (separator == std::string_view::npos) {
                return false;
            }
            const auto name = field.substr(0U, separator);
            const auto value = field.substr(separator + 1U);
            if (name == "password" && !has_password) {
                has_password = decode_form_component(value, password);
                if (!has_password) {
                    return false;
                }
            } else {
                return false;
            }
            if (field_end == std::string_view::npos) {
                break;
            }
            field_start = field_end + 1U;
        }
        return has_password;
    }

    bool device_password_matches(const std::string_view password) const noexcept
    {
        NetworkConfiguration configuration;
        {
            std::lock_guard lock{mutex_};
            configuration = configuration_;
        }
        return constant_time_equal(password, configuration.device_password.data());
    }

    std::array<char, 192U> create_http_session() noexcept
    {
        std::array<std::uint8_t, HttpSessionState::token_characters / 2U> random{};
        esp_fill_random(random.data(), random.size());
        constexpr char hexadecimal[] = "0123456789abcdef";

        std::array<char, 192U> cookie{};
        {
            std::lock_guard lock{mutex_};
            std::array<char, HttpSessionState::token_characters + 1U> token{};
            for (std::size_t index = 0U; index < random.size(); ++index) {
                token[index * 2U] = hexadecimal[random[index] >> 4U];
                token[index * 2U + 1U] = hexadecimal[random[index] & 0x0FU];
            }
            static_cast<void>(session_state_.replace(token.data(), esp_timer_get_time()));
            std::snprintf(
                cookie.data(),
                cookie.size(),
                "%s=%s; Path=/; HttpOnly; SameSite=Lax",
                session_cookie_name,
                token.data()
            );
        }
        return cookie;
    }

    void invalidate_http_session() noexcept
    {
        std::lock_guard lock{mutex_};
        session_state_.invalidate();
    }

    bool session_cookie_valid(httpd_req_t* const request) noexcept
    {
        const auto cookie_length = httpd_req_get_hdr_value_len(request, "Cookie");
        if (cookie_length == 0U || cookie_length >= 384U) {
            return false;
        }
        std::array<char, 384U> cookies{};
        if (httpd_req_get_hdr_value_str(
                request, "Cookie", cookies.data(), cookies.size()
            ) != ESP_OK) {
            return false;
        }

        std::string_view provided;
        std::string_view remaining{cookies.data()};
        while (!remaining.empty()) {
            const auto end = remaining.find(';');
            auto cookie = remaining.substr(0U, end);
            while (!cookie.empty() && cookie.front() == ' ') {
                cookie.remove_prefix(1U);
            }
            const auto separator = cookie.find('=');
            if (separator != std::string_view::npos
                && cookie.substr(0U, separator) == session_cookie_name) {
                provided = cookie.substr(separator + 1U);
                break;
            }
            if (end == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(end + 1U);
        }
        if (provided.size() != HttpSessionState::token_characters) {
            return false;
        }

        std::lock_guard lock{mutex_};
        return session_state_.validate(provided, esp_timer_get_time());
    }

    esp_err_t handle_login_post(httpd_req_t* const request) noexcept
    {
        const auto peer = request_peer_key(request);
        const auto now = esp_timer_get_time();
        if (!login_rate_limiter_.permit(peer, now)) {
            return send_login_rate_limited(
                request, login_rate_limiter_.retry_after_seconds(peer, now)
            );
        }
        std::array<char, 64U> password{};
        if (!read_login_form(request, password)
            || !device_password_matches(password.data())) {
            login_rate_limiter_.record_failure(peer, now);
            return send_login_page(request, true);
        }

        login_rate_limiter_.record_success(peer);
        const auto cookie = create_http_session();
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "303 See Other"));
        static_cast<void>(httpd_resp_set_hdr(request, "Location", "/"));
        static_cast<void>(httpd_resp_set_hdr(request, "Set-Cookie", cookie.data()));
        static_cast<void>(httpd_resp_set_type(request, "text/plain; charset=utf-8"));
        constexpr std::string_view body = "Autentificare reușită.";
        return httpd_resp_send(
            request, body.data(), static_cast<ssize_t>(body.size())
        );
    }

    esp_err_t handle_auth_session_post(httpd_req_t* const request) noexcept
    {
        const auto peer = request_peer_key(request);
        const auto now = esp_timer_get_time();
        if (!login_rate_limiter_.permit(peer, now)) {
            return send_api_rate_limited(
                request, login_rate_limiter_.retry_after_seconds(peer, now)
            );
        }
        bool too_large = false;
        auto document = read_json_body(request, too_large);
        if (too_large) {
            return send_error(request, "413 Payload Too Large", "body depășește 512 bytes");
        }
        if (!document || !has_exact_fields(document.get(), {"password"}, {"password"})) {
            login_rate_limiter_.record_failure(peer, now);
            return send_error(request, "400 Bad Request", "schemă JSON invalidă");
        }
        const auto* const password = cJSON_GetObjectItemCaseSensitive(
            document.get(), "password"
        );
        if (!cJSON_IsString(password) || !device_password_matches(password->valuestring)) {
            login_rate_limiter_.record_failure(peer, now);
            return send_error(request, "401 Unauthorized", "parolă invalidă");
        }
        login_rate_limiter_.record_success(peer);
        const auto cookie = create_http_session();
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "204 No Content"));
        static_cast<void>(httpd_resp_set_hdr(request, "Set-Cookie", cookie.data()));
        return httpd_resp_send(request, nullptr, 0);
    }

    esp_err_t handle_logout(httpd_req_t* const request) noexcept
    {
        invalidate_http_session();
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "204 No Content"));
        static_cast<void>(httpd_resp_set_hdr(
            request,
            "Set-Cookie",
            "smoker_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0"
        ));
        return httpd_resp_send(request, nullptr, 0);
    }

    esp_err_t handle_password_put(httpd_req_t* const request) noexcept
    {
        bool too_large = false;
        auto document = read_json_body(request, too_large);
        if (too_large) {
            return send_error(request, "413 Payload Too Large", "body depășește 512 bytes");
        }
        if (!document || !has_exact_fields(
                document.get(),
                {"current_password", "new_password"},
                {"current_password", "new_password"}
            )) {
            return send_error(request, "400 Bad Request", "schemă JSON invalidă");
        }
        const auto* const current = cJSON_GetObjectItemCaseSensitive(
            document.get(), "current_password"
        );
        const auto* const replacement = cJSON_GetObjectItemCaseSensitive(
            document.get(), "new_password"
        );
        if (!cJSON_IsString(current) || !cJSON_IsString(replacement)) {
            return send_error(request, "400 Bad Request", "tip JSON invalid");
        }
        const std::string_view replacement_value{replacement->valuestring};
        if (replacement_value.size() < 8U || replacement_value.size() > 63U) {
            return send_error(request, "400 Bad Request", "lungime parolă invalidă");
        }
        if (!device_password_matches(current->valuestring)) {
            return send_error(request, "403 Forbidden", "parola curentă este invalidă");
        }
        NetworkConfiguration updated;
        {
            std::lock_guard lock{mutex_};
            updated = configuration_;
        }
        if (!copy_bounded(updated.device_password, replacement_value)) {
            return send_error(request, "400 Bad Request", "lungime parolă invalidă");
        }
        updated.device_password_is_initial = false;
        if (!persist_authentication_configuration(updated)) {
            return send_error(request, "503 Service Unavailable", "NVS indisponibil");
        }
        {
            std::lock_guard lock{mutex_};
            configuration_.device_password = updated.device_password;
            configuration_.device_password_is_initial = false;
        }
        return handle_logout(request);
    }

    bool authenticate(httpd_req_t* const request) noexcept
    {
        if (session_cookie_valid(request)) {
            return true;
        }
        static_cast<void>(send_unauthorized(request));
        return false;
    }

    bool send_unauthorized(httpd_req_t* const request) const noexcept
    {
        if (std::string_view{request->uri}.find("/api/") != 0U) {
            static_cast<void>(send_login_redirect(request));
            return false;
        }
        add_response_headers(request);
        static_cast<void>(send_error(request, "401 Unauthorized", "autentificare necesară"));
        return false;
    }

    bool same_origin(
        httpd_req_t* const request, const HttpRequestScope scope
    ) const noexcept
    {
        if (!request_host_allowed(request, scope)) {
            return false;
        }
        const auto origin_length = httpd_req_get_hdr_value_len(request, "Origin");
        if (origin_length >= 192U) {
            return false;
        }
        std::array<char, 160U> host{};
        if (httpd_req_get_hdr_value_str(
                request, "Host", host.data(), host.size()
            ) != ESP_OK) {
            return false;
        }
        std::optional<std::string_view> origin_value;
        std::array<char, 192U> origin{};
        if (origin_length > 0U) {
            if (httpd_req_get_hdr_value_str(
                    request, "Origin", origin.data(), origin.size()
                ) != ESP_OK) {
                return false;
            }
            origin_value = std::string_view{origin.data()};
        }
        const bool state_changing = request->method == HTTP_POST
            || request->method == HTTP_PUT || request->method == HTTP_DELETE;
        return http_origin_allowed(
            host.data(),
            origin_value,
            state_changing
        );
    }

    [[nodiscard]] std::uint32_t request_peer_key(
        httpd_req_t* const request
    ) const noexcept
    {
        const int socket = httpd_req_to_sockfd(request);
        sockaddr_storage peer{};
        socklen_t length = sizeof(peer);
        if (socket < 0
            || getpeername(
                   socket, reinterpret_cast<sockaddr*>(&peer), &length
               ) != 0) {
            return 0U;
        }
        if (peer.ss_family == AF_INET) {
            const auto* const ipv4 = reinterpret_cast<const sockaddr_in*>(&peer);
            return ntohl(ipv4->sin_addr.s_addr);
        }
        if (peer.ss_family == AF_INET6) {
            const auto* const ipv6 = reinterpret_cast<const sockaddr_in6*>(&peer);
            return extract_ipv4_mapped_address(
                std::span<const std::uint8_t, 16U>{ipv6->sin6_addr.s6_addr, 16U}
            ).value_or(0U);
        }
        return 0U;
    }

    esp_err_t send_asset(
        httpd_req_t* const request,
        const char* const content_type,
        const std::string_view content
    ) const noexcept
    {
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_type(request, content_type));
        return httpd_resp_send(
            request, content.data(), static_cast<ssize_t>(content.size())
        );
    }

    esp_err_t send_error(
        httpd_req_t* const request,
        const char* const status,
        const char* const message
    ) const noexcept
    {
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, status));
        static_cast<void>(httpd_resp_set_type(request, "application/json"));
        const auto body = build_http_error_body(message);
        return httpd_resp_send(
            request, body.bytes.data(), static_cast<ssize_t>(body.length)
        );
    }

    esp_err_t send_json(
        httpd_req_t* const request,
        cJSON* const value,
        const char* const status = "200 OK"
    ) const noexcept
    {
        char* const serialized = cJSON_PrintUnformatted(value);
        if (serialized == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, status));
        static_cast<void>(httpd_resp_set_type(request, "application/json"));
        const auto result = httpd_resp_send(
            request, serialized, static_cast<ssize_t>(std::strlen(serialized))
        );
        cJSON_free(serialized);
        return result;
    }

    CjsonPointer read_json_body(httpd_req_t* const request, bool& too_large) const noexcept
    {
        too_large = request->content_len > static_cast<int>(maximum_body_bytes);
        if (too_large || request->content_len <= 0) {
            return {};
        }
        std::array<char, 64U> content_type{};
        if (httpd_req_get_hdr_value_str(
                request, "Content-Type", content_type.data(), content_type.size()
            ) != ESP_OK
            || !http_content_type_matches(content_type.data(), "application/json")) {
            return {};
        }
        std::array<char, maximum_body_bytes + 1U> body{};
        int received_total = 0;
        std::size_t receive_timeouts = 0U;
        const auto receive_deadline =
            esp_timer_get_time() + body_receive_deadline_microseconds;
        while (received_total < request->content_len) {
            if (esp_timer_get_time() >= receive_deadline) {
                return {};
            }
            const auto received = httpd_req_recv(
                request,
                body.data() + received_total,
                static_cast<std::size_t>(request->content_len)
                    - static_cast<std::size_t>(received_total)
            );
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                if (receive_timeouts++ >= maximum_body_receive_timeouts) {
                    return {};
                }
                continue;
            }
            if (received <= 0) {
                return {};
            }
            received_total += received;
            if (esp_timer_get_time() >= receive_deadline
                && received_total < request->content_len) {
                return {};
            }
        }
        body[static_cast<std::size_t>(received_total)] = '\0';
        const char* end = nullptr;
        CjsonPointer result{cJSON_ParseWithLengthOpts(
            body.data(), static_cast<std::size_t>(received_total) + 1U, &end, true
        )};
        if (!result || end != body.data() + received_total) {
            return {};
        }
        return result;
    }

    esp_err_t handle_snapshot(httpd_req_t* const request) const noexcept
    {
        if (!control_ready_.load(std::memory_order_acquire)) {
            return send_error(request, "503 Service Unavailable", "controlul nu este pregătit");
        }
        auto lease = snapshots_.acquire();
        if (!lease) {
            return send_error(request, "503 Service Unavailable", "snapshot indisponibil");
        }
        const auto snapshot = lease.view();
        CjsonPointer root{cJSON_CreateObject()};
        if (!root) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        bool json_valid = true;

        auto* const session = cJSON_AddObjectToObject(root.get(), "session");
        if (session == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddStringToObject(
            session, "status", session_status_name(snapshot.session_status)
        ), json_valid);
        if (snapshot.session_id) {
            require_json(cJSON_AddNumberToObject(
                session, "id", static_cast<double>(*snapshot.session_id)
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(session, "id"), json_valid);
        }
        require_json(cJSON_AddStringToObject(
            session, "stop_reason", stop_reason_name(snapshot.stop_reason)
        ), json_valid);

        auto* const chamber = cJSON_AddObjectToObject(root.get(), "chamber");
        if (chamber == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(add_optional_temperature(
            chamber, "current_celsius", snapshot.chamber_temperature
        ), json_valid);
        require_json(add_optional_temperature(
            chamber, "target_celsius", snapshot.chamber_target
        ), json_valid);
        auto* const heater = cJSON_AddObjectToObject(root.get(), "heater");
        if (heater == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddNumberToObject(
            heater, "demand_percent", static_cast<double>(snapshot.heater_demand.percent())
        ), json_valid);
        require_json(cJSON_AddStringToObject(heater, "io", "SIMULATED"), json_valid);

        auto* const timer = cJSON_AddObjectToObject(root.get(), "timer");
        if (timer == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddBoolToObject(timer, "started", snapshot.timer.started), json_valid);
        require_json(cJSON_AddBoolToObject(timer, "completed", snapshot.timer.completed), json_valid);
        require_json(cJSON_AddNumberToObject(
            timer, "elapsed_ms", static_cast<double>(snapshot.timer.elapsed.count())
        ), json_valid);

        auto* const probes = cJSON_AddArrayToObject(root.get(), "probes");
        if (probes == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        for (const auto& probe : snapshot.probes) {
            CjsonPointer item{cJSON_CreateObject()};
            if (!item) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            bool item_valid = true;
            require_json(cJSON_AddNumberToObject(
                item.get(), "id", static_cast<double>(probe.id)
            ), item_valid);
            require_json(cJSON_AddStringToObject(item.get(), "name", probe.name.data()), item_valid);
            require_json(cJSON_AddStringToObject(
                item.get(), "role", probe_role_name(probe.role)
            ), item_valid);
            require_json(add_optional_temperature(
                item.get(), "current_celsius", probe.current_temperature
            ), item_valid);
            require_json(add_optional_temperature(
                item.get(), "target_celsius", probe.target_temperature
            ), item_valid);
            require_json(cJSON_AddBoolToObject(item.get(), "enabled", probe.enabled), item_valid);
            require_json(cJSON_AddBoolToObject(
                item.get(), "alarm_enabled", probe.alarm_enabled
            ), item_valid);
            if (!item_valid || cJSON_AddItemToArray(probes, item.get()) == 0) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            static_cast<void>(item.release());
        }

        auto* const alarms = cJSON_AddArrayToObject(root.get(), "alarms");
        if (alarms == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        for (const auto& alarm : snapshot.active_alarms) {
            CjsonPointer item{cJSON_CreateObject()};
            if (!item) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            bool item_valid = true;
            require_json(cJSON_AddNumberToObject(
                item.get(), "id", static_cast<double>(alarm.id)
            ), item_valid);
            require_json(cJSON_AddStringToObject(
                item.get(), "code", alarm_code_name(alarm.code)
            ), item_valid);
            if (alarm.probe_id) {
                require_json(cJSON_AddNumberToObject(
                    item.get(), "probe_id", static_cast<double>(*alarm.probe_id)
                ), item_valid);
            } else {
                require_json(cJSON_AddNullToObject(item.get(), "probe_id"), item_valid);
            }
            require_json(cJSON_AddBoolToObject(
                item.get(), "acknowledged", alarm.acknowledged
            ), item_valid);
            if (!item_valid || cJSON_AddItemToArray(alarms, item.get()) == 0) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            static_cast<void>(item.release());
        }

        if (snapshot.active_fault) {
            auto* const fault = cJSON_AddObjectToObject(root.get(), "fault");
            if (fault == nullptr) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            require_json(cJSON_AddStringToObject(
                fault, "code", fault_code_name(snapshot.active_fault->code)
            ), json_valid);
            require_json(cJSON_AddBoolToObject(
                fault, "latched", snapshot.active_fault->latched
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(root.get(), "fault"), json_valid);
        }
        auto* const limits = cJSON_AddObjectToObject(root.get(), "limits");
        if (limits == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(add_optional_temperature(
            limits, "maximum_chamber_celsius", snapshot.maximum_chamber_temperature
        ), json_valid);
        auto* const counters = cJSON_AddObjectToObject(root.get(), "counters");
        if (counters == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddNumberToObject(
            counters,
            "application_command_overflow",
            static_cast<double>(snapshot.command_queue_overflow_count)
        ), json_valid);
        require_json(cJSON_AddNumberToObject(
            counters,
            "transport_command_overflow",
            static_cast<double>(command_mailbox_.overflow_count())
        ), json_valid);
        require_json(cJSON_AddNumberToObject(
            counters,
            "snapshot_publish_dropped",
            static_cast<double>(snapshots_.dropped_publish_count())
        ), json_valid);

        auto* const command_results = cJSON_AddArrayToObject(root.get(), "command_results");
        if (command_results == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        for (const auto& result : snapshot.command_results) {
            CjsonPointer item{cJSON_CreateObject()};
            if (!item) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            bool item_valid = true;
            require_json(cJSON_AddNumberToObject(
                item.get(), "id", static_cast<double>(result.correlation_id)
            ), item_valid);
            require_json(cJSON_AddBoolToObject(
                item.get(), "semantic_accepted", result.semantic_accepted
            ), item_valid);
            if (!item_valid || cJSON_AddItemToArray(command_results, item.get()) == 0) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            static_cast<void>(item.release());
        }
        require_json(cJSON_AddBoolToObject(
            root.get(), "firmware_update_active", snapshot.firmware_update_active
        ), json_valid);
        require_json(cJSON_AddBoolToObject(root.get(), "simulated_io", true), json_valid);
        if (!json_valid) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get());
    }

    esp_err_t handle_firmware_get(httpd_req_t* const request) const noexcept
    {
        const auto status = firmware_updates_.status();
        CjsonPointer root{cJSON_CreateObject()};
        if (!root) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        bool valid = true;
        require_json(cJSON_AddStringToObject(
            root.get(), "state", firmware_update_state_name(status.state)
        ), valid);
        require_json(cJSON_AddStringToObject(
            root.get(), "current_version", status.current_version.data()
        ), valid);
        if (status.available_version[0] == '\0') {
            require_json(cJSON_AddNullToObject(root.get(), "available_version"), valid);
        } else {
            require_json(cJSON_AddStringToObject(
                root.get(), "available_version", status.available_version.data()
            ), valid);
        }
        require_json(cJSON_AddNumberToObject(
            root.get(), "progress_percent", static_cast<double>(status.progress_percent)
        ), valid);
        require_json(cJSON_AddBoolToObject(
            root.get(), "installation_allowed", status.installation_allowed
        ), valid);
        if (status.error[0] == '\0') {
            require_json(cJSON_AddNullToObject(root.get(), "error"), valid);
        } else {
            require_json(cJSON_AddStringToObject(
                root.get(), "error", status.error.data()
            ), valid);
        }
        if (!valid) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get());
    }

    esp_err_t handle_firmware_check(httpd_req_t* const request) noexcept
    {
        if (request->content_len != 0) {
            return send_error(request, "400 Bad Request", "verificarea nu acceptă body");
        }
        if (!firmware_updates_.request_check()) {
            return send_error(request, "409 Conflict", "operație firmware activă");
        }
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "202 Accepted"));
        static_cast<void>(httpd_resp_set_type(request, "application/json"));
        return httpd_resp_send(
            request,
            firmware_check_accepted_body.data(),
            static_cast<ssize_t>(firmware_check_accepted_body.size())
        );
    }

    esp_err_t handle_firmware_install(httpd_req_t* const request) noexcept
    {
        if (!control_ready_.load(std::memory_order_acquire)) {
            return send_error(request, "503 Service Unavailable", "controlul nu este pregătit");
        }
        bool too_large = false;
        auto document = read_json_body(request, too_large);
        if (too_large) {
            return send_error(request, "413 Payload Too Large", "body depășește 512 bytes");
        }
        if (!document || !has_exact_fields(document.get(), {"version"}, {"version"})) {
            return send_error(request, "400 Bad Request", "schemă firmware invalidă");
        }
        const auto* const version = cJSON_GetObjectItemCaseSensitive(
            document.get(), "version"
        );
        if (!cJSON_IsString(version)
            || !SemanticVersion::parse(version->valuestring)) {
            return send_error(request, "400 Bad Request", "versiune semantică invalidă");
        }

        const auto correlation_id = next_command_correlation_id_++;
        if (next_command_correlation_id_ == 0U) {
            next_command_correlation_id_ = 1U;
        }
        const auto response = build_http_command_admission_body(correlation_id);
        if (response.length == 0U) {
            return send_error(request, "503 Service Unavailable", "răspuns indisponibil");
        }
        const auto admission = firmware_updates_.request_install(
            version->valuestring, correlation_id
        );
        switch (admission) {
        case FirmwareInstallAdmission::Running:
            return send_error(request, "409 Conflict", "opriți sesiunea înainte de instalare");
        case FirmwareInstallAdmission::VersionMismatch:
            return send_error(request, "409 Conflict", "versiunea disponibilă s-a schimbat");
        case FirmwareInstallAdmission::BusyOrUnavailable:
            return send_error(request, "409 Conflict", "instalarea nu este disponibilă");
        case FirmwareInstallAdmission::Accepted:
            break;
        }

        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "202 Accepted"));
        static_cast<void>(httpd_resp_set_type(request, "application/json"));
        return httpd_resp_send(
            request, response.bytes.data(), static_cast<ssize_t>(response.length)
        );
    }

    esp_err_t handle_network_get(httpd_req_t* const request) const noexcept
    {
        NetworkConfiguration configuration;
        NetworkStatus status;
        {
            std::lock_guard lock{mutex_};
            configuration = configuration_;
            status = status_;
        }
        CjsonPointer root{cJSON_CreateObject()};
        if (!root) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        bool json_valid = true;
        require_json(cJSON_AddStringToObject(
            root.get(), "hostname", hostname_.data()
        ), json_valid);
        require_json(cJSON_AddBoolToObject(
            root.get(), "default_password_warning", default_password_warning()
        ), json_valid);
        auto* const sta = cJSON_AddObjectToObject(root.get(), "sta");
        if (sta == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddBoolToObject(
            sta, "configured", configuration.has_sta_credentials
        ), json_valid);
        require_json(cJSON_AddBoolToObject(
            sta, "connected", status.sta_connected
        ), json_valid);
        require_json(cJSON_AddStringToObject(
            sta, "ssid", configuration.ssid.data()
        ), json_valid);
        if (status.sta_connected) {
            require_json(cJSON_AddStringToObject(
                sta, "ip", status.sta_ip.data()
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(sta, "ip"), json_valid);
        }
        if (status.sta_last_error[0] != '\0') {
            require_json(cJSON_AddStringToObject(
                sta, "last_error", status.sta_last_error.data()
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(sta, "last_error"), json_valid);
        }
        auto* const ap = cJSON_AddObjectToObject(root.get(), "ap");
        if (ap == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        require_json(cJSON_AddBoolToObject(ap, "active", status.ap_active), json_valid);
        require_json(cJSON_AddStringToObject(ap, "ssid", ap_ssid_.data()), json_valid);
        require_json(cJSON_AddStringToObject(ap, "ip", "192.168.4.1"), json_valid);
        if (!json_valid) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get());
    }

    esp_err_t handle_setup_status(httpd_req_t* const request) const noexcept
    {
        NetworkConfiguration configuration;
        NetworkStatus status;
        {
            std::lock_guard lock{mutex_};
            configuration = configuration_;
            status = status_;
        }
        CjsonPointer root{cJSON_CreateObject()};
        if (!root) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        bool json_valid = true;
        require_json(cJSON_AddStringToObject(root.get(), "mode", "commissioning"), json_valid);
        require_json(cJSON_AddStringToObject(root.get(), "ap_ssid", ap_ssid_.data()), json_valid);
        require_json(cJSON_AddStringToObject(root.get(), "hostname", hostname_.data()), json_valid);
        require_json(cJSON_AddBoolToObject(
            root.get(), "sta_configured", configuration.has_sta_credentials
        ), json_valid);
        require_json(cJSON_AddBoolToObject(
            root.get(), "sta_connected", status.sta_connected
        ), json_valid);
        if (status.sta_last_error[0] != '\0') {
            require_json(cJSON_AddStringToObject(
                root.get(), "last_error", status.sta_last_error.data()
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(root.get(), "last_error"), json_valid);
        }
        if (!json_valid) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get());
    }

    esp_err_t handle_scan_post(httpd_req_t* const request) noexcept
    {
        const bool combined = request_network_scan();
        CjsonPointer root{cJSON_CreateObject()};
        if (!root
            || cJSON_AddStringToObject(root.get(), "status", "accepted") == nullptr
            || cJSON_AddBoolToObject(root.get(), "combined", combined) == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get(), "202 Accepted");
    }

    esp_err_t handle_scan_get(httpd_req_t* const request) const noexcept
    {
        WifiScanState state = WifiScanState::Idle;
        WifiNetworkList results;
        {
            std::lock_guard lock{mutex_};
            state = scan_coordinator_.state();
            results = scan_results_;
        }

        CjsonPointer root{cJSON_CreateObject()};
        if (!root) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        bool json_valid = true;
        require_json(cJSON_AddStringToObject(
            root.get(), "state", wifi_scan_state_name(state)
        ), json_valid);
        auto* const networks = cJSON_AddArrayToObject(root.get(), "networks");
        if (networks == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        for (std::size_t index = 0U; index < results.count; ++index) {
            const auto& network = results.networks[index];
            CjsonPointer item{cJSON_CreateObject()};
            if (!item) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            bool item_valid = true;
            require_json(cJSON_AddStringToObject(
                item.get(), "ssid", network.ssid.data()
            ), item_valid);
            require_json(cJSON_AddNumberToObject(
                item.get(), "rssi_dbm", static_cast<double>(network.rssi_dbm)
            ), item_valid);
            require_json(cJSON_AddNumberToObject(
                item.get(), "channel", static_cast<double>(network.channel)
            ), item_valid);
            require_json(cJSON_AddStringToObject(
                item.get(), "security", wifi_security_category_name(network.security)
            ), item_valid);
            require_json(cJSON_AddBoolToObject(
                item.get(), "supported", wifi_security_is_supported(network.security)
            ), item_valid);
            if (!item_valid || cJSON_AddItemToArray(networks, item.get()) == 0) {
                return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
            }
            static_cast<void>(item.release());
        }
        require_json(cJSON_AddBoolToObject(
            root.get(), "truncated", results.truncated
        ), json_valid);
        if (state == WifiScanState::Error) {
            require_json(cJSON_AddStringToObject(
                root.get(), "error", "scan_failed"
            ), json_valid);
        } else {
            require_json(cJSON_AddNullToObject(root.get(), "error"), json_valid);
        }
        if (!json_valid) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, root.get());
    }

    esp_err_t handle_network_put(httpd_req_t* const request) noexcept
    {
        bool too_large = false;
        auto document = read_json_body(request, too_large);
        if (too_large) {
            return send_error(request, "413 Payload Too Large", "body depășește 512 bytes");
        }
        if (!document || !has_exact_fields(
                document.get(), {"ssid", "wifi_password"}, {"ssid", "wifi_password"}
            )) {
            return send_error(request, "400 Bad Request", "schemă JSON invalidă");
        }
        const auto* const ssid = cJSON_GetObjectItemCaseSensitive(document.get(), "ssid");
        const auto* const wifi_password = cJSON_GetObjectItemCaseSensitive(
            document.get(), "wifi_password"
        );
        if (!cJSON_IsString(ssid) || !cJSON_IsString(wifi_password)) {
            return send_error(request, "400 Bad Request", "tip JSON invalid");
        }
        const std::string_view ssid_value{ssid->valuestring};
        const std::string_view wifi_password_value{wifi_password->valuestring};
        if (ssid_value.empty() || ssid_value.size() > 32U
            || wifi_password_value.size() < 8U || wifi_password_value.size() > 63U) {
            return send_error(request, "400 Bad Request", "lungime credentiale invalidă");
        }
        {
            std::lock_guard lock{mutex_};
            for (std::size_t index = 0U; index < scan_results_.count; ++index) {
                const auto& network = scan_results_.networks[index];
                if (ssid_value == network.ssid.data()
                    && !wifi_security_is_supported(network.security)) {
                    return send_error(
                        request,
                        "400 Bad Request",
                        "rețeaua necesită WPA2/WPA3 Personal"
                    );
                }
            }
        }

        NetworkConfiguration updated;
        {
            std::lock_guard lock{mutex_};
            updated = configuration_;
        }
        if (!copy_bounded(updated.ssid, ssid_value)
            || !copy_bounded(updated.wifi_password, wifi_password_value)) {
            return send_error(request, "400 Bad Request", "credentiale prea lungi");
        }
        updated.has_sta_credentials = true;
        std::unique_lock mode_lock{wifi_mode_mutex_};
        {
            std::lock_guard lock{mutex_};
            sta_configuration_transition_ = true;
        }
        if (!persist_wifi_configuration(updated)) {
            {
                std::lock_guard lock{mutex_};
                sta_configuration_transition_ = false;
            }
            return send_error(request, "503 Service Unavailable", "NVS indisponibil");
        }
        {
            std::lock_guard lock{mutex_};
            configuration_ = updated;
            status_.sta_connected = false;
            status_.sta_ipv4 = 0U;
            status_.sta_ip.fill('\0');
            status_.sta_last_error.fill('\0');
            sta_connect_in_flight_ = false;
        }
        const bool reconnecting = apply_updated_wifi_configuration_locked();
        mode_lock.unlock();

        CjsonPointer response{cJSON_CreateObject()};
        if (!response
            || cJSON_AddStringToObject(response.get(), "status", "accepted") == nullptr
            || cJSON_AddBoolToObject(
                   response.get(), "reconnecting", reconnecting
               ) == nullptr) {
            return send_error(request, "503 Service Unavailable", "memorie JSON indisponibilă");
        }
        return send_json(request, response.get(), "202 Accepted");
    }

    [[nodiscard]] bool persist_wifi_configuration(
        const NetworkConfiguration& configuration
    ) noexcept
    {
        const auto persisted = encode_wifi_configuration(configuration);
        return nvs_set_blob(
                   nvs_handle_, nvs_wifi_configuration_key,
                   &persisted, sizeof(persisted)
               ) == ESP_OK
            && nvs_commit(nvs_handle_) == ESP_OK;
    }

    [[nodiscard]] bool persist_authentication_configuration(
        const NetworkConfiguration& configuration
    ) noexcept
    {
        const auto persisted = encode_authentication_configuration(configuration);
        return nvs_set_blob(
                   nvs_handle_, nvs_authentication_configuration_key,
                   &persisted, sizeof(persisted)
               ) == ESP_OK
            && nvs_commit(nvs_handle_) == ESP_OK;
    }

    [[nodiscard]] bool apply_updated_wifi_configuration() noexcept
    {
        std::lock_guard mode_lock{wifi_mode_mutex_};
        {
            std::lock_guard lock{mutex_};
            sta_configuration_transition_ = true;
        }
        return apply_updated_wifi_configuration_locked();
    }

    [[nodiscard]] bool apply_updated_wifi_configuration_locked() noexcept
    {
        NetworkConfiguration configuration;
        {
            std::lock_guard lock{mutex_};
            if (scan_coordinator_.blocks_sta_reconnect()) {
                wifi_apply_after_scan_ = true;
                scan_coordinator_.note_sta_disconnected(true);
                return true;
            }
            configuration = configuration_;
        }
        wifi_config_t sta{};
        std::memcpy(sta.sta.ssid, configuration.ssid.data(), std::strlen(configuration.ssid.data()));
        std::memcpy(
            sta.sta.password,
            configuration.wifi_password.data(),
            std::strlen(configuration.wifi_password.data())
        );
        sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta.sta.pmf_cfg.capable = true;
        sta.sta.pmf_cfg.required = false;

        esp_err_t apply_status = ESP_OK;
        bool connect_after_disconnect_event = false;
        bool driver_configuration_applied = false;
        bool keep_ap = false;
        {
            std::lock_guard lock{mutex_};
            keep_ap = status_.ap_active && !status_.sta_connected;
        }
        if (keep_ap) {
            if (!enable_soft_ap_locked(true, false)) {
                apply_status = ESP_FAIL;
            }
        } else {
            apply_status = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if (apply_status == ESP_OK) {
            apply_status = esp_wifi_set_config(WIFI_IF_STA, &sta);
            if (apply_status == ESP_OK) {
                driver_configuration_applied = true;
                std::lock_guard lock{mutex_};
                sta_retry_state_.note_configuration_applied();
            }
        }
        if (apply_status == ESP_OK) {
            {
                std::lock_guard lock{mutex_};
                configuration_disconnect_pending_ = true;
            }
            const auto disconnect_status = esp_wifi_disconnect();
            connect_after_disconnect_event = disconnect_status == ESP_OK;
            if (!connect_after_disconnect_event
                && disconnect_status != ESP_ERR_WIFI_NOT_CONNECT) {
                apply_status = disconnect_status;
            }
            if (disconnect_status != ESP_OK) {
                std::lock_guard lock{mutex_};
                configuration_disconnect_pending_ = false;
            }
        }
        if (apply_status == ESP_OK && !connect_after_disconnect_event) {
            {
                std::lock_guard lock{mutex_};
                sta_connect_in_flight_ = true;
            }
            apply_status = esp_wifi_connect();
            if (apply_status == ESP_OK && sta_connect_retry_timer_ != nullptr) {
                static_cast<void>(esp_timer_stop(sta_connect_retry_timer_));
            } else if (apply_status != ESP_OK) {
                std::lock_guard lock{mutex_};
                sta_connect_in_flight_ = false;
            }
        }
        if (apply_status == ESP_OK && connect_after_disconnect_event) {
            schedule_sta_connect_retry();
        }
        if (apply_status != ESP_OK) {
            if (!driver_configuration_applied) {
                std::lock_guard lock{mutex_};
                sta_retry_state_.note_configuration_apply_failed();
            }
            ESP_LOGE(
                tag,
                "Saved STA configuration could not be applied immediately: %s",
                esp_err_to_name(apply_status)
            );
            static_cast<void>(enable_soft_ap_locked(true, false));
            schedule_sta_connect_retry();
        }
        {
            std::lock_guard lock{mutex_};
            sta_configuration_transition_ = false;
        }
        schedule_fallback();
        return apply_status == ESP_OK;
    }

    esp_err_t handle_command(httpd_req_t* const request) noexcept
    {
        if (!control_ready_.load(std::memory_order_acquire)) {
            return send_error(request, "503 Service Unavailable", "controlul nu este pregătit");
        }
        bool too_large = false;
        auto document = read_json_body(request, too_large);
        if (too_large) {
            return send_error(request, "413 Payload Too Large", "body depășește 512 bytes");
        }
        const auto* const type = document
            ? cJSON_GetObjectItemCaseSensitive(document.get(), "type")
            : nullptr;
        if (!document || !cJSON_IsString(type)) {
            return send_error(request, "400 Bad Request", "schemă JSON invalidă");
        }

        std::optional<app::Command> command;
        const std::string_view type_name{type->valuestring};
        if (type_name == "start_session") {
            if (!has_exact_fields(document.get(), {"type", "target_celsius"}, {"type", "target_celsius"})) {
                return send_error(request, "400 Bad Request", "schemă start_session invalidă");
            }
            bool valid = false;
            const auto target = parse_temperature_or_null(
                cJSON_GetObjectItemCaseSensitive(document.get(), "target_celsius"), valid
            );
            if (!valid) {
                return send_error(request, "400 Bad Request", "temperatură invalidă");
            }
            auto recipe = startup_recipe_;
            recipe.stage.chamber_target = target;
            command.emplace(app::StartSessionCommand{next_session_id_++, std::move(recipe)});
            if (next_session_id_ == 0U) {
                next_session_id_ = 1U;
            }
        } else if (type_name == "stop_session") {
            if (!has_exact_fields(document.get(), {"type"}, {"type"})) {
                return send_error(request, "400 Bad Request", "schemă stop_session invalidă");
            }
            command.emplace(app::StopSessionCommand{});
        } else if (type_name == "set_chamber_target") {
            if (!has_exact_fields(document.get(), {"type", "target_celsius"}, {"type", "target_celsius"})) {
                return send_error(request, "400 Bad Request", "schemă target invalidă");
            }
            bool valid = false;
            const auto target = parse_temperature_or_null(
                cJSON_GetObjectItemCaseSensitive(document.get(), "target_celsius"), valid
            );
            if (!valid) {
                return send_error(request, "400 Bad Request", "temperatură invalidă");
            }
            command.emplace(app::SetChamberTargetCommand{target});
        } else if (type_name == "set_probe_target") {
            if (!has_exact_fields(
                    document.get(),
                    {"type", "probe_id", "target_celsius"},
                    {"type", "probe_id", "target_celsius"}
                )) {
                return send_error(request, "400 Bad Request", "schemă sondă invalidă");
            }
            const auto probe_id = parse_unsigned_integer<core::ProbeId>(
                cJSON_GetObjectItemCaseSensitive(document.get(), "probe_id")
            );
            bool valid = false;
            const auto target = parse_temperature_or_null(
                cJSON_GetObjectItemCaseSensitive(document.get(), "target_celsius"), valid
            );
            if (!probe_id || !valid) {
                return send_error(request, "400 Bad Request", "valori sondă invalide");
            }
            command.emplace(app::SetProbeTargetCommand{*probe_id, target});
        } else if (type_name == "set_probe_enabled"
                   || type_name == "set_probe_alarm_enabled") {
            if (!has_exact_fields(
                    document.get(),
                    {"type", "probe_id", "enabled"},
                    {"type", "probe_id", "enabled"}
                )) {
                return send_error(request, "400 Bad Request", "schemă sondă invalidă");
            }
            const auto probe_id = parse_unsigned_integer<core::ProbeId>(
                cJSON_GetObjectItemCaseSensitive(document.get(), "probe_id")
            );
            const auto* const enabled = cJSON_GetObjectItemCaseSensitive(document.get(), "enabled");
            if (!probe_id || !cJSON_IsBool(enabled)) {
                return send_error(request, "400 Bad Request", "valori sondă invalide");
            }
            if (type_name == "set_probe_enabled") {
                command.emplace(app::SetProbeEnabledCommand{
                    *probe_id, cJSON_IsTrue(enabled) != 0
                });
            } else {
                command.emplace(app::SetProbeAlarmEnabledCommand{
                    *probe_id, cJSON_IsTrue(enabled) != 0
                });
            }
        } else if (type_name == "acknowledge_alarm") {
            if (!has_exact_fields(document.get(), {"type", "alarm_id"}, {"type", "alarm_id"})) {
                return send_error(request, "400 Bad Request", "schemă alarmă invalidă");
            }
            const auto alarm_id = parse_unsigned_integer<core::AlarmId>(
                cJSON_GetObjectItemCaseSensitive(document.get(), "alarm_id")
            );
            if (!alarm_id) {
                return send_error(request, "400 Bad Request", "alarm_id invalid");
            }
            command.emplace(app::AcknowledgeAlarmCommand{*alarm_id});
        } else if (type_name == "clear_resolved_fault") {
            if (!has_exact_fields(document.get(), {"type"}, {"type"})) {
                return send_error(request, "400 Bad Request", "schemă fault invalidă");
            }
            command.emplace(app::ClearResolvedFaultCommand{});
        } else {
            return send_error(request, "400 Bad Request", "tip comandă necunoscut");
        }

        const auto correlation_id = next_command_correlation_id_++;
        if (next_command_correlation_id_ == 0U) {
            next_command_correlation_id_ = 1U;
        }
        const auto response = build_http_command_admission_body(correlation_id);
        if (response.length == 0U) {
            return send_error(
                request, "503 Service Unavailable", "răspuns admission indisponibil"
            );
        }
        const auto admission = command_mailbox_.push(
            std::move(*command), correlation_id
        );
        if (admission == app::MailboxAdmission::Full) {
            return send_error(request, "429 Too Many Requests", "mailbox plin");
        }
        add_response_headers(request);
        static_cast<void>(httpd_resp_set_status(request, "202 Accepted"));
        static_cast<void>(httpd_resp_set_type(request, "application/json"));
        return httpd_resp_send(
            request,
            response.bytes.data(),
            static_cast<ssize_t>(response.length)
        );
    }

    [[nodiscard]] bool default_password_warning() const noexcept
    {
        std::lock_guard lock{mutex_};
        return configuration_.device_password_is_initial;
    }

    app::SpscCommandMailbox& command_mailbox_;
    const app::SnapshotExchange& snapshots_;
    FirmwareUpdateService& firmware_updates_;
    core::Recipe startup_recipe_;
    mutable std::mutex mutex_;
    std::mutex wifi_mode_mutex_;
    NetworkConfiguration configuration_;
    NetworkStatus status_;
    WifiScanCoordinator scan_coordinator_;
    WifiFallbackCoordinator fallback_coordinator_;
    WifiStaRetryState sta_retry_state_;
    WifiNetworkList scan_results_;
    HttpSessionState session_state_;
    LoginRateLimiter login_rate_limiter_;
    std::array<wifi_ap_record_t, maximum_scan_records> wifi_scan_records_{};
    std::array<RawWifiNetwork, maximum_scan_records> raw_scan_records_{};
    std::array<char, 20U> ap_ssid_{};
    std::array<char, 24U> hostname_{};
    std::atomic_bool control_ready_{false};
    std::atomic_bool dns_running_{false};
    std::atomic_bool dns_task_exited_{false};
    std::atomic_int dns_socket_{-1};
    std::atomic<TaskHandle_t> dns_task_{nullptr};
    StaticTask_t dns_task_storage_{};
    std::array<StackType_t, dns_stack_bytes / sizeof(StackType_t)> dns_stack_{};
    std::array<std::uint8_t, dns_packet_bytes> dns_request_{};
    std::array<std::uint8_t, dns_packet_bytes> dns_response_{};
    core::SessionId next_session_id_{1U};
    std::uint32_t next_command_correlation_id_{1U};
    nvs_handle_t nvs_handle_{0U};
    esp_netif_t* sta_netif_{nullptr};
    esp_netif_t* ap_netif_{nullptr};
    esp_event_handler_instance_t wifi_handler_{nullptr};
    esp_event_handler_instance_t ip_handler_{nullptr};
    esp_timer_handle_t fallback_timer_{nullptr};
    esp_timer_handle_t sta_only_retry_timer_{nullptr};
    esp_timer_handle_t sta_connect_retry_timer_{nullptr};
    esp_timer_handle_t scan_timeout_timer_{nullptr};
    httpd_handle_t http_server_{nullptr};
    bool wifi_started_{false};
    std::atomic_bool wifi_running_{false};
    bool mdns_started_{false};
    bool scan_driver_active_{false};
    bool wifi_apply_after_scan_{false};
    bool sta_configuration_transition_{false};
    bool sta_connect_in_flight_{false};
    bool configuration_disconnect_pending_{false};
};

LocalConnectivityService::LocalConnectivityService(
    app::SpscCommandMailbox& command_mailbox,
    const app::SnapshotExchange& snapshots,
    FirmwareUpdateService& firmware_updates,
    core::Recipe startup_recipe
) noexcept
    : impl_{new (std::nothrow) Impl{
          command_mailbox, snapshots, firmware_updates, std::move(startup_recipe)
      }}
{
}

LocalConnectivityService::~LocalConnectivityService() = default;

bool LocalConnectivityService::start() noexcept
{
    return impl_ != nullptr && impl_->start();
}

void LocalConnectivityService::mark_control_ready() noexcept
{
    if (impl_ != nullptr) {
        impl_->mark_control_ready();
    }
}

} // namespace smoker::platform

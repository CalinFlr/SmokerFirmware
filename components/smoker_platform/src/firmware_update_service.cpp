#include "smoker/platform/firmware_update_service.hpp"

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string_view>

#include <strings.h>

namespace smoker::platform {
namespace {

using namespace std::chrono_literals;

constexpr char tag[] = "smoker_ota";
constexpr std::size_t ota_task_stack_size_bytes = 16U * 1024U;
constexpr UBaseType_t ota_task_priority = tskIDLE_PRIORITY + 1U;
constexpr std::uint32_t internal_validation_correlation_id = 0xFFFFFFFEU;
constexpr std::int64_t validation_timeout_microseconds = 10LL * 1000LL * 1000LL;
constexpr std::int64_t permission_timeout_microseconds = 10LL * 1000LL * 1000LL;
constexpr std::int64_t check_timeout_microseconds = 30LL * 1000LL * 1000LL;
constexpr std::int64_t install_timeout_microseconds = 5LL * 60LL * 1000LL * 1000LL;
constexpr std::uint32_t required_validation_cycles = 5U;
constexpr int maximum_http_wait_milliseconds = 5'000;
constexpr std::size_t maximum_redirect_location_length = 2048U;
constexpr std::size_t maximum_http_request_line_overhead = 16U;
constexpr std::size_t http_request_buffer_size = 4096U;
constexpr std::size_t download_buffer_size = 4096U;
constexpr std::uint32_t maximum_redirects = 5U;
constexpr std::time_t minimum_trusted_wall_time = 1'700'000'000;

template <std::size_t Size>
void copy_bounded_text(
    std::array<char, Size>& destination,
    const std::string_view source
) noexcept
{
    destination.fill('\0');
    const auto length = std::min(source.size(), destination.size() - 1U);
    std::memcpy(destination.data(), source.data(), length);
}

static_assert(ota_task_stack_size_bytes % sizeof(StackType_t) == 0U);
static_assert(sizeof(esp_image_header_t) == firmware_image_header_size);
static_assert(sizeof(esp_image_segment_header_t) == firmware_segment_header_size);
static_assert(sizeof(esp_app_desc_t) == firmware_app_descriptor_size);
static_assert(
    http_request_buffer_size
        >= maximum_redirect_location_length + maximum_http_request_line_overhead,
    "OTA TX buffer must fit an accepted redirect plus GET/protocol framing"
);
static_assert(
    http_request_buffer_size
        <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
    "ESP HTTP client TX buffer size must fit its int configuration field"
);
static_assert(offsetof(esp_image_header_t, chip_id) == 12U);
static_assert(offsetof(esp_app_desc_t, version) == 16U);
static_assert(offsetof(esp_app_desc_t, project_name) == 48U);
static_assert(
    static_cast<std::uint16_t>(ESP_CHIP_ID_ESP32S3) == firmware_target_chip_id
);

// OtaTask executes the SPI-flash OTA APIs. Its stack and TCB must remain
// accessible while the flash cache (and therefore PSRAM) is unavailable.
// Keep them out of the heap-owned Impl even when malloc prefers external RAM.
DRAM_ATTR StaticTask_t ota_task_storage;
DRAM_ATTR std::array<StackType_t, ota_task_stack_size_bytes / sizeof(StackType_t)>
    ota_task_stack;

class OtaFlashLease final {
public:
    OtaFlashLease(
        FlashOperationCoordinator& coordinator,
        const MonotonicDeadline& deadline
    ) noexcept
        : coordinator_{coordinator}
    {
        while (!(acquired_ = coordinator_.try_acquire_ota())) {
            if (deadline.expired(esp_timer_get_time())) return;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ~OtaFlashLease()
    {
        if (acquired_) {
            coordinator_.release_ota();
        } else {
            // try_acquire_ota() raises the deferral flag before waiting for a
            // bounded in-progress history operation. If the OTA deadline wins,
            // allow history to resume even though this lease never owned flash.
            coordinator_.set_history_deferred(false);
        }
    }
    OtaFlashLease(const OtaFlashLease&) = delete;
    OtaFlashLease& operator=(const OtaFlashLease&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return acquired_; }

private:
    FlashOperationCoordinator& coordinator_;
    bool acquired_{false};
};

enum class ImageStreamError : std::uint8_t {
    None,
    Timeout,
    InsecureRedirect,
    TooManyRedirects,
    HttpFailure,
};

bool is_redirect_status(const int status) noexcept
{
    return status == 301 || status == 302 || status == 303 || status == 307
        || status == 308;
}

bool starts_with_https(const std::string_view url) noexcept
{
    return url.starts_with("https://");
}

struct RedirectCapture final {
    std::array<char, maximum_redirect_location_length> location{};
    bool present{false};
    bool overflow{false};

    void reset() noexcept
    {
        location.fill('\0');
        present = false;
        overflow = false;
    }
};

esp_err_t capture_redirect_header(esp_http_client_event_t* const event) noexcept
{
    if (event == nullptr || event->event_id != HTTP_EVENT_ON_HEADER
        || event->header_key == nullptr || event->header_value == nullptr
        || strcasecmp(event->header_key, "Location") != 0) {
        return ESP_OK;
    }
    auto* const capture = static_cast<RedirectCapture*>(event->user_data);
    if (capture == nullptr) {
        return ESP_OK;
    }
    capture->present = true;
    const std::string_view location{event->header_value};
    if (location.size() >= capture->location.size()) {
        capture->overflow = true;
        return ESP_OK;
    }
    std::memcpy(capture->location.data(), location.data(), location.size());
    return ESP_OK;
}

bool synchronize_wall_time(const MonotonicDeadline& deadline) noexcept
{
    std::time_t now = 0;
    std::time(&now);
    if (now >= minimum_trusted_wall_time) {
        return true;
    }

    esp_sntp_config_t configuration = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    const auto init_status = esp_netif_sntp_init(&configuration);
    if (init_status != ESP_OK) {
        ESP_LOGE(tag, "SNTP initialization failed: %s", esp_err_to_name(init_status));
        return false;
    }
    const auto wait_milliseconds = deadline.remaining_milliseconds(
        esp_timer_get_time(), 10'000
    );
    if (wait_milliseconds <= 0) {
        esp_netif_sntp_deinit();
        return false;
    }
    const auto sync_status = esp_netif_sntp_sync_wait(
        pdMS_TO_TICKS(wait_milliseconds)
    );
    esp_netif_sntp_deinit();
    if (sync_status != ESP_OK) {
        ESP_LOGE(tag, "SNTP synchronization timed out: %s", esp_err_to_name(sync_status));
        return false;
    }
    std::time(&now);
    return now >= minimum_trusted_wall_time;
}

esp_http_client_config_t make_http_configuration(RedirectCapture& redirects) noexcept
{
    esp_http_client_config_t configuration{};
    configuration.url = firmware_release_url;
    configuration.event_handler = &capture_redirect_header;
    configuration.user_data = &redirects;
    configuration.crt_bundle_attach = esp_crt_bundle_attach;
    configuration.timeout_ms = maximum_http_wait_milliseconds;
    configuration.buffer_size_tx = static_cast<int>(http_request_buffer_size);
    configuration.disable_auto_redirect = true;
    configuration.keep_alive_enable = true;
    configuration.max_redirection_count = static_cast<int>(maximum_redirects);
    return configuration;
}

const char* stream_error_code(
    const ImageStreamError error,
    const bool installing
) noexcept
{
    switch (error) {
    case ImageStreamError::Timeout:
        return installing ? "installation_timeout" : "check_timeout";
    case ImageStreamError::InsecureRedirect:
        return "insecure_redirect";
    case ImageStreamError::TooManyRedirects:
        return "redirect_limit_exceeded";
    case ImageStreamError::None:
    case ImageStreamError::HttpFailure:
        return installing ? "installation_download_failed" : "descriptor_download_failed";
    }
    return installing ? "installation_download_failed" : "descriptor_download_failed";
}

class HttpsFirmwareStream final {
public:
    explicit HttpsFirmwareStream(MonotonicDeadline deadline) noexcept
        : deadline_{deadline}
    {
    }

    ~HttpsFirmwareStream()
    {
        if (client_ != nullptr) {
            esp_http_client_cleanup(client_);
        }
    }

    HttpsFirmwareStream(const HttpsFirmwareStream&) = delete;
    HttpsFirmwareStream& operator=(const HttpsFirmwareStream&) = delete;

    [[nodiscard]] bool open() noexcept
    {
        auto configuration = make_http_configuration(redirect_capture_);
        client_ = esp_http_client_init(&configuration);
        if (client_ == nullptr) {
            error_ = ImageStreamError::HttpFailure;
            return false;
        }

        std::uint32_t redirects = 0U;
        while (true) {
            redirect_capture_.reset();
            if (!set_remaining_timeout()
                || esp_http_client_open(client_, 0) != ESP_OK) {
                set_io_error();
                return false;
            }
            if (esp_http_client_get_transport_type(client_)
                != HTTP_TRANSPORT_OVER_SSL) {
                error_ = ImageStreamError::InsecureRedirect;
                ESP_LOGE(tag, "Rejected non-HTTPS OTA connection");
                return false;
            }
            if (!set_remaining_timeout()) {
                return false;
            }
            const auto header_result = esp_http_client_fetch_headers(client_);
            if (header_result < 0) {
                set_io_error();
                return false;
            }

            const auto status = esp_http_client_get_status_code(client_);
            if (!is_redirect_status(status)) {
                if (status != 200) {
                    ESP_LOGE(tag, "Firmware source returned HTTP status %d", status);
                    error_ = ImageStreamError::HttpFailure;
                    return false;
                }
                content_length_ = esp_http_client_is_chunked_response(client_)
                    ? -1
                    : esp_http_client_get_content_length(client_);
                return true;
            }

            if (redirects >= maximum_redirects) {
                error_ = ImageStreamError::TooManyRedirects;
                ESP_LOGE(tag, "Firmware source exceeded the redirect limit");
                return false;
            }
            if (!redirect_capture_.present || redirect_capture_.overflow) {
                error_ = ImageStreamError::HttpFailure;
                ESP_LOGE(tag, "Firmware redirect Location is missing or too large");
                return false;
            }
            const std::string_view location{redirect_capture_.location.data()};
            if (location.find("://") != std::string_view::npos
                && !starts_with_https(location)) {
                error_ = ImageStreamError::InsecureRedirect;
                ESP_LOGE(tag, "Rejected non-HTTPS OTA redirect");
                return false;
            }
            if (esp_http_client_set_redirection(client_) != ESP_OK
                || esp_http_client_get_transport_type(client_)
                    != HTTP_TRANSPORT_OVER_SSL) {
                error_ = ImageStreamError::InsecureRedirect;
                ESP_LOGE(tag, "Rejected invalid or non-HTTPS OTA redirect");
                return false;
            }
            ++redirects;
            esp_http_client_close(client_);
        }
    }

    [[nodiscard]] bool read_exact(std::span<std::uint8_t> destination) noexcept
    {
        std::size_t offset = 0U;
        while (offset < destination.size()) {
            const auto count = read(destination.subspan(offset));
            if (count <= 0) {
                return false;
            }
            offset += static_cast<std::size_t>(count);
        }
        return true;
    }

    [[nodiscard]] int read(std::span<std::uint8_t> destination) noexcept
    {
        while (true) {
            if (!set_remaining_timeout()) {
                return -1;
            }
            const auto count = esp_http_client_read(
                client_,
                reinterpret_cast<char*>(destination.data()),
                static_cast<int>(destination.size())
            );
            if (count == -ESP_ERR_HTTP_EAGAIN) {
                continue;
            }
            if (count < 0) {
                error_ = ImageStreamError::HttpFailure;
            }
            return count;
        }
    }

    [[nodiscard]] bool complete_data_received() const noexcept
    {
        return client_ != nullptr && esp_http_client_is_complete_data_received(client_);
    }

    [[nodiscard]] std::int64_t content_length() const noexcept
    {
        return content_length_;
    }

    [[nodiscard]] ImageStreamError error() const noexcept
    {
        return error_;
    }

private:
    [[nodiscard]] bool set_remaining_timeout() noexcept
    {
        const auto wait = deadline_.remaining_milliseconds(
            esp_timer_get_time(), maximum_http_wait_milliseconds
        );
        if (wait <= 0) {
            error_ = ImageStreamError::Timeout;
            return false;
        }
        if (esp_http_client_set_timeout_ms(client_, wait) != ESP_OK) {
            error_ = ImageStreamError::HttpFailure;
            return false;
        }
        return true;
    }

    void set_io_error() noexcept
    {
        error_ = deadline_.expired(esp_timer_get_time())
            ? ImageStreamError::Timeout
            : ImageStreamError::HttpFailure;
    }

    MonotonicDeadline deadline_;
    RedirectCapture redirect_capture_{};
    esp_http_client_handle_t client_{nullptr};
    std::int64_t content_length_{-1};
    ImageStreamError error_{ImageStreamError::None};
};

class OtaWriteSession final {
public:
    ~OtaWriteSession()
    {
        if (active_) {
            static_cast<void>(esp_ota_abort(handle_));
        }
    }

    [[nodiscard]] bool begin(const esp_partition_t* const partition) noexcept
    {
        if (partition == nullptr
            || esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle_) != ESP_OK) {
            return false;
        }
        active_ = true;
        return true;
    }

    [[nodiscard]] bool write_chunk(
        const std::span<const std::uint8_t> bytes
    ) noexcept
    {
        return active_ && esp_ota_write(handle_, bytes.data(), bytes.size()) == ESP_OK;
    }

    [[nodiscard]] esp_err_t finish() noexcept
    {
        if (!active_) {
            return ESP_ERR_INVALID_STATE;
        }
        active_ = false;
        return esp_ota_end(handle_);
    }

    void abort() noexcept
    {
        if (active_) {
            active_ = false;
            static_cast<void>(esp_ota_abort(handle_));
        }
    }

private:
    esp_ota_handle_t handle_{0U};
    bool active_{false};
};

std::optional<FirmwareImageMetadata> read_image_metadata(
    HttpsFirmwareStream& stream,
    std::array<std::uint8_t, firmware_metadata_prefix_size>& prefix
) noexcept
{
    if (!stream.read_exact(prefix)) {
        return std::nullopt;
    }
    return parse_firmware_image_metadata(prefix);
}

[[noreturn]] void rollback_running_firmware_and_reboot() noexcept
{
    const auto status = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(tag, "Rollback API returned unexpectedly: %s", esp_err_to_name(status));
    esp_restart();
    while (true) {
        vTaskDelay(portMAX_DELAY);
    }
}

} // namespace

bool running_firmware_validation_pending() noexcept
{
    const auto* const running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state{};
    return running != nullptr
        && esp_ota_get_state_partition(running, &image_state) == ESP_OK
        && image_state == ESP_OTA_IMG_PENDING_VERIFY;
}

void rollback_pending_firmware_and_reboot_if_needed() noexcept
{
    if (!running_firmware_validation_pending()) {
        return;
    }
    ESP_LOGE(tag, "Critical startup failed for a pending image; rolling back");
    rollback_running_firmware_and_reboot();
}

class FirmwareUpdateService::Impl final {
public:
    Impl(
        const app::SnapshotExchange& snapshots,
        FlashOperationCoordinator& flash_operations
    ) noexcept
        : snapshots_{snapshots}
        , flash_operations_{flash_operations}
        , coordinator_{esp_app_get_description()->version}
    {
        if (running_firmware_validation_pending()) {
            flash_operations_.set_history_deferred(true);
            coordinator_.begin_validation();
            validation_pending_.store(true, std::memory_order_release);
            validation_started_at_.store(esp_timer_get_time(), std::memory_order_release);
            prepare_correlation_.store(
                internal_validation_correlation_id, std::memory_order_release
            );
        }
    }

    ~Impl()
    {
        running_.store(false, std::memory_order_release);
        const auto task = task_.load(std::memory_order_acquire);
        if (task != nullptr) {
            xTaskNotifyGive(task);
        }
    }

    bool start() noexcept
    {
        if (task_.load(std::memory_order_acquire) != nullptr) {
            return true;
        }
        running_.store(true, std::memory_order_release);
        const auto task = xTaskCreateStaticPinnedToCore(
            &Impl::task_entry,
            "OtaTask",
            static_cast<std::uint32_t>(ota_task_stack_size_bytes),
            this,
            ota_task_priority,
            ota_task_stack.data(),
            &ota_task_storage,
            0
        );
        if (task == nullptr) {
            running_.store(false, std::memory_order_release);
            ESP_LOGE(tag, "Could not create static OtaTask");
            if (validation_pending_.load(std::memory_order_acquire)) {
                rollback_running_firmware_and_reboot();
            }
            {
                std::lock_guard lock{mutex_};
                coordinator_.fail("ota_task_unavailable");
            }
            return false;
        }
        task_.store(task, std::memory_order_release);
        return true;
    }

    FirmwareUpdateStatus status() const noexcept
    {
        std::lock_guard lock{mutex_};
        auto result = coordinator_.status();
        auto lease = snapshots_.acquire();
        if (!lease || lease.view().session_status == core::SessionStatus::Running
            || lease.view().firmware_update_active) {
            result.installation_allowed = false;
        }
        return result;
    }

    bool request_check() noexcept
    {
        if (!worker_available()) {
            return false;
        }
        {
            std::lock_guard lock{mutex_};
            if (!coordinator_.begin_check()) {
                return false;
            }
            check_requested_.store(true, std::memory_order_release);
        }
        notify();
        return true;
    }

    FirmwareInstallAdmission request_install(
        const std::string_view version,
        const std::uint32_t permission_correlation_id
    ) noexcept
    {
        if (!worker_available()) {
            return FirmwareInstallAdmission::BusyOrUnavailable;
        }
        auto lease = snapshots_.acquire();
        if (!lease) {
            return FirmwareInstallAdmission::BusyOrUnavailable;
        }
        const auto snapshot = lease.view();
        if (snapshot.session_status == core::SessionStatus::Running) {
            return FirmwareInstallAdmission::Running;
        }

        std::lock_guard lock{mutex_};
        const auto& current = coordinator_.status();
        if (current.state != FirmwareUpdateState::Available) {
            return FirmwareInstallAdmission::BusyOrUnavailable;
        }
        if (version != current.available_version.data()) {
            return FirmwareInstallAdmission::VersionMismatch;
        }
        if (!coordinator_.begin_install(
                version, snapshot.session_status, permission_correlation_id
            )) {
            return FirmwareInstallAdmission::BusyOrUnavailable;
        }
        std::uint32_t empty_prepare_signal = 0U;
        if (!prepare_correlation_.compare_exchange_strong(
                empty_prepare_signal,
                permission_correlation_id,
                std::memory_order_release,
                std::memory_order_relaxed
            )) {
            coordinator_.cancel_install(permission_correlation_id);
            return FirmwareInstallAdmission::BusyOrUnavailable;
        }
        permission_deadline_.emplace(
            esp_timer_get_time(), permission_timeout_microseconds
        );
        notify();
        return FirmwareInstallAdmission::Accepted;
    }

    bool consume_prepare_request(std::uint32_t& correlation_id) noexcept
    {
        correlation_id = prepare_correlation_.exchange(
            0U, std::memory_order_acq_rel
        );
        return correlation_id != 0U;
    }

    bool consume_finish_request() noexcept
    {
        return finish_requested_.exchange(false, std::memory_order_acq_rel);
    }

    void retry_prepare_request(const std::uint32_t correlation_id) noexcept
    {
        if (correlation_id != 0U) {
            prepare_correlation_.store(
                correlation_id, std::memory_order_release
            );
        }
    }

    void retry_finish_request() noexcept
    {
        finish_requested_.store(true, std::memory_order_release);
    }

    void publish_control_cycle(
        const app::SmokerSnapshotView& snapshot,
        const bool watchdog_reset_succeeded
    ) noexcept
    {
        if (!validation_pending_.load(std::memory_order_acquire)) {
            return;
        }
        const bool good = snapshot.firmware_update_active
            && snapshot.session_status == core::SessionStatus::Idle
            && snapshot.chamber_temperature.has_value()
            && !snapshot.active_fault
            && snapshot.heater_demand == core::HeaterDemand::off()
            && watchdog_reset_succeeded;
        if (snapshot.active_fault) {
            validation_fault_.store(true, std::memory_order_release);
        }
        if (good) {
            validation_cycles_.fetch_add(1U, std::memory_order_acq_rel);
        } else {
            validation_cycles_.store(0U, std::memory_order_release);
        }
        notify();
    }

private:
    [[nodiscard]] bool worker_available() const noexcept
    {
        return running_.load(std::memory_order_acquire)
            && task_.load(std::memory_order_acquire) != nullptr;
    }

    static void task_entry(void* const parameter) noexcept
    {
        static_cast<Impl*>(parameter)->run();
    }

    void notify() const noexcept
    {
        const auto task = task_.load(std::memory_order_acquire);
        if (task != nullptr) {
            xTaskNotifyGive(task);
        }
    }

    void run() noexcept
    {
        ESP_LOGI(tag, "OtaTask started on core %d; TWDT subscription disabled", xPortGetCoreID());
        while (running_.load(std::memory_order_acquire)) {
            inspect_application_permission();
            inspect_permission_timeout();
            inspect_pending_validation();

            if (check_requested_.exchange(false, std::memory_order_acq_rel)) {
                perform_check();
            }

            bool install = false;
            {
                std::lock_guard lock{mutex_};
                install = coordinator_.installation_ready();
            }
            if (install) {
                perform_install();
            }
            static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)));
        }
        task_.store(nullptr, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    void inspect_application_permission() noexcept
    {
        auto lease = snapshots_.acquire();
        if (!lease) {
            return;
        }
        std::lock_guard lock{mutex_};
        coordinator_.observe_application_snapshot(lease.view());
        if (coordinator_.status().state != FirmwareUpdateState::WaitingPermission) {
            permission_deadline_.reset();
        }
        flush_finish_signal_locked();
    }

    void inspect_permission_timeout() noexcept
    {
        std::lock_guard lock{mutex_};
        if (coordinator_.status().state != FirmwareUpdateState::WaitingPermission
            || !permission_deadline_
            || !permission_deadline_->expired(esp_timer_get_time())) {
            return;
        }
        ESP_LOGE(tag, "Application firmware permission timed out");
        coordinator_.fail("permission_timeout");
        permission_deadline_.reset();
        flush_finish_signal_locked();
    }

    void inspect_pending_validation() noexcept
    {
        if (!validation_pending_.load(std::memory_order_acquire)) {
            return;
        }
        const auto started_at = validation_started_at_.load(std::memory_order_acquire);
        const bool timed_out = started_at > 0
            && esp_timer_get_time() - started_at >= validation_timeout_microseconds;
        if (validation_fault_.load(std::memory_order_acquire) || timed_out) {
            ESP_LOGE(tag, "Pending image validation failed; rolling back");
            rollback_running_firmware_and_reboot();
        }
        if (validation_cycles_.load(std::memory_order_acquire)
            < required_validation_cycles) {
            return;
        }
        const auto status = esp_ota_mark_app_valid_cancel_rollback();
        if (status != ESP_OK) {
            ESP_LOGE(tag, "Could not mark pending image valid: %s", esp_err_to_name(status));
            rollback_running_firmware_and_reboot();
        }
        validation_pending_.store(false, std::memory_order_release);
        flash_operations_.set_history_deferred(false);
        {
            std::lock_guard lock{mutex_};
            coordinator_.note_validation_succeeded();
            flush_finish_signal_locked();
        }
        ESP_LOGI(tag, "Pending image marked valid after five safe control cycles");
    }

    void perform_check() noexcept
    {
        const MonotonicDeadline deadline{
            esp_timer_get_time(), check_timeout_microseconds
        };
        if (!synchronize_wall_time(deadline)) {
            fail("time_sync_failed");
            return;
        }
        HttpsFirmwareStream stream{deadline};
        if (!stream.open()) {
            fail(stream_error_code(stream.error(), false));
            return;
        }
        std::array<std::uint8_t, firmware_metadata_prefix_size> prefix{};
        const auto metadata = read_image_metadata(stream, prefix);
        if (!metadata) {
            fail(stream.error() == ImageStreamError::None
                    ? "descriptor_invalid"
                    : stream_error_code(stream.error(), false));
            return;
        }
        if (stream.content_length() >= 0
            && stream.content_length()
                < static_cast<std::int64_t>(firmware_metadata_prefix_size)) {
            fail("descriptor_invalid");
            return;
        }
        if (deadline.expired(esp_timer_get_time())) {
            fail("check_timeout");
            return;
        }
        std::lock_guard lock{mutex_};
        coordinator_.complete_check(metadata->descriptor());
    }

    void perform_install() noexcept
    {
        const MonotonicDeadline deadline{
            esp_timer_get_time(), install_timeout_microseconds
        };
        OtaFlashLease flash_lease{flash_operations_, deadline};
        if (!flash_lease) {
            fail("flash_operation_timeout");
            return;
        }
        if (!synchronize_wall_time(deadline)) {
            fail("time_sync_failed");
            return;
        }

        HttpsFirmwareStream stream{deadline};
        if (!stream.open()) {
            fail(stream_error_code(stream.error(), true));
            return;
        }

        std::array<std::uint8_t, firmware_metadata_prefix_size> prefix{};
        const auto metadata = read_image_metadata(stream, prefix);
        if (!metadata) {
            fail(stream.error() == ImageStreamError::None
                    ? "installation_descriptor_failed"
                    : stream_error_code(stream.error(), true));
            return;
        }
        bool descriptor_allowed = false;
        {
            std::lock_guard lock{mutex_};
            const auto current = SemanticVersion::parse(
                coordinator_.status().current_version.data()
            );
            const auto descriptor = metadata->descriptor();
            descriptor_allowed = current
                && validate_firmware_descriptor(descriptor, *current)
                    == FirmwareDescriptorDecision::Newer
                && descriptor.version == coordinator_.status().available_version.data();
        }
        if (!descriptor_allowed) {
            fail("installation_descriptor_changed");
            return;
        }

        const auto* const update_partition = esp_ota_get_next_update_partition(nullptr);
        if (update_partition == nullptr) {
            fail("installation_partition_unavailable");
            return;
        }
        const auto content_length = stream.content_length();
        if (content_length >= 0
            && (content_length < static_cast<std::int64_t>(prefix.size())
                || content_length > static_cast<std::int64_t>(update_partition->size))) {
            fail("installation_image_size_invalid");
            return;
        }

        OtaWriteSession writer;
        if (!writer.begin(update_partition) || !writer.write_chunk(prefix)) {
            writer.abort();
            fail("installation_flash_failed");
            return;
        }

        std::size_t total_received = prefix.size();
        std::array<std::uint8_t, download_buffer_size> buffer{};
        while (true) {
            const auto count = stream.read(buffer);
            if (count < 0) {
                writer.abort();
                fail(stream_error_code(stream.error(), true));
                return;
            }
            if (count == 0) {
                break;
            }
            const auto received = static_cast<std::size_t>(count);
            if (received > update_partition->size - total_received
                || !writer.write_chunk(
                    std::span<const std::uint8_t>{buffer.data(), received}
                )) {
                writer.abort();
                fail("installation_flash_failed");
                return;
            }
            total_received += received;
            if (content_length > 0) {
                const auto percentage = static_cast<std::uint8_t>(std::min<std::int64_t>(
                    (static_cast<std::int64_t>(total_received) * 100LL) / content_length,
                    100LL
                ));
                std::lock_guard lock{mutex_};
                coordinator_.note_install_progress(percentage);
            }
        }
        if (!stream.complete_data_received()
            || (content_length >= 0
                && total_received != static_cast<std::size_t>(content_length))) {
            writer.abort();
            fail("installation_download_incomplete");
            return;
        }
        if (deadline.expired(esp_timer_get_time())) {
            writer.abort();
            fail("installation_timeout");
            return;
        }

        const auto finish_status = writer.finish();
        if (finish_status != ESP_OK) {
            fail("installation_image_invalid");
            return;
        }
        if (deadline.expired(esp_timer_get_time())) {
            fail("installation_timeout");
            return;
        }
        const auto boot_status = esp_ota_set_boot_partition(update_partition);
        if (boot_status != ESP_OK) {
            fail("installation_boot_selection_failed");
            return;
        }
        {
            std::lock_guard lock{mutex_};
            coordinator_.note_rebooting();
        }
        ESP_LOGI(tag, "OTA image verified and selected; rebooting");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }

    void fail(const std::string_view error) noexcept
    {
        std::lock_guard lock{mutex_};
        coordinator_.fail(error);
        permission_deadline_.reset();
        flush_finish_signal_locked();
    }

    void flush_finish_signal_locked() noexcept
    {
        if (coordinator_.consume_finish_signal()) {
            finish_requested_.store(true, std::memory_order_release);
        }
    }

    const app::SnapshotExchange& snapshots_;
    FlashOperationCoordinator& flash_operations_;
    mutable std::mutex mutex_;
    FirmwareUpdateCoordinator coordinator_;
    std::optional<MonotonicDeadline> permission_deadline_;
    std::atomic_bool running_{false};
    std::atomic_bool check_requested_{false};
    std::atomic_bool finish_requested_{false};
    std::atomic<std::uint32_t> prepare_correlation_{0U};
    std::atomic_bool validation_pending_{false};
    std::atomic_bool validation_fault_{false};
    std::atomic<std::uint32_t> validation_cycles_{0U};
    std::atomic<std::int64_t> validation_started_at_{0};
    std::atomic<TaskHandle_t> task_{nullptr};
};

FirmwareUpdateService::FirmwareUpdateService(
    const app::SnapshotExchange& snapshots,
    FlashOperationCoordinator& flash_operations
) noexcept
    : impl_{new (std::nothrow) Impl{snapshots, flash_operations}}
{
}

FirmwareUpdateService::~FirmwareUpdateService() = default;

bool FirmwareUpdateService::start() noexcept
{
    if (impl_ != nullptr) {
        return impl_->start();
    }
    ESP_LOGE(tag, "OTA service allocation failed");
    rollback_pending_firmware_and_reboot_if_needed();
    return false;
}

FirmwareUpdateStatus FirmwareUpdateService::status() const noexcept
{
    if (impl_ != nullptr) {
        return impl_->status();
    }
    FirmwareUpdateStatus result{};
    result.state = FirmwareUpdateState::Failed;
    copy_bounded_text(result.error, "ota_service_allocation_failed");
    const auto* const description = esp_app_get_description();
    if (description != nullptr) {
        copy_bounded_text(result.current_version, description->version);
    }
    return result;
}

bool FirmwareUpdateService::request_check() noexcept
{
    return impl_ != nullptr && impl_->request_check();
}

FirmwareInstallAdmission FirmwareUpdateService::request_install(
    const std::string_view version,
    const std::uint32_t permission_correlation_id
) noexcept
{
    return impl_ != nullptr
        ? impl_->request_install(version, permission_correlation_id)
        : FirmwareInstallAdmission::BusyOrUnavailable;
}

bool FirmwareUpdateService::consume_prepare_request(
    std::uint32_t& correlation_id
) noexcept
{
    return impl_ != nullptr && impl_->consume_prepare_request(correlation_id);
}

bool FirmwareUpdateService::consume_finish_request() noexcept
{
    return impl_ != nullptr && impl_->consume_finish_request();
}

void FirmwareUpdateService::retry_prepare_request(
    const std::uint32_t correlation_id
) noexcept
{
    if (impl_ != nullptr) {
        impl_->retry_prepare_request(correlation_id);
    }
}

void FirmwareUpdateService::retry_finish_request() noexcept
{
    if (impl_ != nullptr) {
        impl_->retry_finish_request();
    }
}

void FirmwareUpdateService::publish_control_cycle(
    const app::SmokerSnapshotView& snapshot,
    const bool watchdog_reset_succeeded
) noexcept
{
    if (impl_ != nullptr) {
        impl_->publish_control_cycle(snapshot, watchdog_reset_succeeded);
    }
}

} // namespace smoker::platform

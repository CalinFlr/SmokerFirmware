#include "smoker/platform/blynk_service.hpp"

#include "smoker/platform/blynk_connection_support.hpp"
#include "smoker/platform/blynk_command_support.hpp"
#include "smoker/platform/blynk_provisioning_support.hpp"
#include "smoker/platform/blynk_remote_support.hpp"

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <string_view>
#include <utility>

namespace smoker::platform {
namespace {

constexpr char tag[] = "smoker_blynk";
constexpr char nvs_namespace[] = "fumuri_blynk";
constexpr char nvs_key[] = "credentials";
constexpr char downlink_prefix[] = "downlink/ds/";
constexpr char downlink_subscription[] = "downlink/ds/#";
constexpr char status_topic[] = "batch_ds";
constexpr char result_topic[] = "ds/LastCommandResult";
constexpr std::uint32_t broker_port = 8883U;
constexpr std::uint32_t keepalive_seconds = 45U;
constexpr std::uint32_t reconnect_timeout_ms = 10'000U;
constexpr std::size_t blynk_task_stack_size_bytes = 12U * 1024U;
constexpr UBaseType_t blynk_task_priority = tskIDLE_PRIORITY + 1U;
constexpr std::int64_t snapshot_period_ms = 1000;
constexpr TickType_t task_poll_ticks = pdMS_TO_TICKS(50);

static_assert(blynk_task_stack_size_bytes % sizeof(StackType_t) == 0U);

DRAM_ATTR StaticTask_t blynk_task_storage;
DRAM_ATTR std::array<StackType_t, blynk_task_stack_size_bytes / sizeof(StackType_t)>
    blynk_task_stack;

[[nodiscard]] std::int64_t monotonic_ms() noexcept
{
    return esp_timer_get_time() / 1000;
}

[[nodiscard]] bool terminal_status(const core::SessionStatus status) noexcept
{
    return status == core::SessionStatus::Stopped
        || status == core::SessionStatus::Fault;
}

[[nodiscard]] bool ota_result_state(const FirmwareUpdateState state) noexcept
{
    return state == FirmwareUpdateState::UpToDate
        || state == FirmwareUpdateState::Available
        || state == FirmwareUpdateState::Rebooting
        || state == FirmwareUpdateState::Failed;
}

} // namespace

class BlynkService::Impl final {
public:
    Impl(
        app::SpscCommandMailbox& application_mailbox,
        const app::SnapshotExchange& snapshots,
        FirmwareUpdateService& firmware_updates,
        RuntimeIdGenerator& ids,
        core::Recipe startup_recipe
    ) noexcept
        : application_mailbox_{application_mailbox}
        , snapshots_{snapshots}
        , firmware_updates_{firmware_updates}
        , ids_{ids}
        , mapper_{std::move(startup_recipe)}
    {
    }

    ~Impl()
    {
        running_.store(false, std::memory_order_release);
        stop_mqtt();
        if (nvs_handle_ != 0U) nvs_close(nvs_handle_);
    }

    [[nodiscard]] bool start() noexcept
    {
        if (task_.load(std::memory_order_acquire) != nullptr) return true;
        if (!initialize_uart() || !initialize_nvs()) return false;
        load_configuration();
        running_.store(true, std::memory_order_release);
        const auto task = xTaskCreateStaticPinnedToCore(
            &Impl::task_entry,
            "BlynkTask",
            static_cast<std::uint32_t>(blynk_task_stack_size_bytes),
            this,
            blynk_task_priority,
            blynk_task_stack.data(),
            &blynk_task_storage,
            0
        );
        if (task == nullptr) {
            running_.store(false, std::memory_order_release);
            ESP_LOGE(tag, "Could not create static BlynkTask");
            return false;
        }
        task_.store(task, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool accepts_connection_generation(
        const std::uint32_t connection_generation
    ) const noexcept
    {
        return connection_boundary_.accepts(connection_generation);
    }

private:
    static void task_entry(void* const parameter) noexcept
    {
        static_cast<Impl*>(parameter)->run();
    }

    static void mqtt_event_entry(
        void* const handler_arguments,
        const esp_event_base_t,
        const std::int32_t event_id,
        void* const event_data
    ) noexcept
    {
        static_cast<Impl*>(handler_arguments)->mqtt_event(
            static_cast<esp_mqtt_event_handle_t>(event_data), event_id
        );
    }

    [[nodiscard]] bool initialize_uart() noexcept
    {
        const auto status = uart_driver_install(
            UART_NUM_0, 512, 0, 0, nullptr, 0
        );
        if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(tag, "UART0 provisioning unavailable: %s", esp_err_to_name(status));
            return false;
        }
        static_cast<void>(uart_set_baudrate(UART_NUM_0, 115200U));
        return true;
    }

    [[nodiscard]] bool initialize_nvs() noexcept
    {
        const auto flash_status = nvs_flash_init();
        if (flash_status != ESP_OK) {
            ESP_LOGE(tag, "Blynk NVS initialization failed: %s", esp_err_to_name(flash_status));
            return false;
        }
        const auto status = nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle_);
        if (status != ESP_OK) {
            ESP_LOGE(tag, "Blynk NVS namespace unavailable: %s", esp_err_to_name(status));
            return false;
        }
        return true;
    }

    void load_configuration() noexcept
    {
        std::array<std::uint8_t, blynk_persisted_blob_size> blob{};
        std::size_t length = blob.size();
        const auto status = nvs_get_blob(nvs_handle_, nvs_key, blob.data(), &length);
        if (status == ESP_ERR_NVS_NOT_FOUND) {
            configured_ = false;
            ESP_LOGI(tag, "Blynk disabled until UART0 provisioning");
            return;
        }
        const auto decoded = status == ESP_OK && length == blob.size()
            ? decode_blynk_configuration(blob) : std::nullopt;
        if (!decoded) {
            configured_ = false;
            ESP_LOGE(tag, "Blynk credential blob invalid; remote access disabled");
            return;
        }
        configuration_ = *decoded;
        configured_ = true;
    }

    [[nodiscard]] bool persist_configuration(
        const BlynkProvisionedConfiguration& configuration
    ) noexcept
    {
        if (!valid_blynk_configuration(configuration)) return false;
        const auto blob = encode_blynk_configuration(configuration);
        return nvs_set_blob(nvs_handle_, nvs_key, blob.data(), blob.size()) == ESP_OK
            && nvs_commit(nvs_handle_) == ESP_OK;
    }

    [[nodiscard]] bool clear_configuration() noexcept
    {
        const auto status = nvs_erase_key(nvs_handle_, nvs_key);
        return (status == ESP_OK || status == ESP_ERR_NVS_NOT_FOUND)
            && nvs_commit(nvs_handle_) == ESP_OK;
    }

    void start_mqtt() noexcept
    {
        if (!configured_ || mqtt_client_ != nullptr) return;
        esp_mqtt_client_config_t mqtt_configuration{};
        mqtt_configuration.broker.address.hostname = configuration_.endpoint.data();
        mqtt_configuration.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
        mqtt_configuration.broker.address.port = broker_port;
        mqtt_configuration.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        mqtt_configuration.credentials.client_id = "";
        mqtt_configuration.credentials.username = "device";
        mqtt_configuration.credentials.authentication.password = configuration_.token.data();
        mqtt_configuration.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
        mqtt_configuration.session.keepalive = keepalive_seconds;
        mqtt_configuration.session.disable_clean_session = false;
        mqtt_configuration.network.reconnect_timeout_ms = reconnect_timeout_ms;
        mqtt_configuration.task.priority = static_cast<int>(blynk_task_priority);
        mqtt_configuration.task.stack_size = 6144;
        mqtt_configuration.buffer.size = static_cast<int>(blynk_public_message_limit);
        mqtt_configuration.buffer.out_size = static_cast<int>(blynk_public_message_limit);

        mqtt_client_ = esp_mqtt_client_init(&mqtt_configuration);
        if (mqtt_client_ == nullptr) {
            ESP_LOGE(tag, "Could not initialize Blynk MQTT client");
            return;
        }
        if (esp_mqtt_client_register_event(
                mqtt_client_, MQTT_EVENT_ANY, &Impl::mqtt_event_entry, this
            ) != ESP_OK
            || esp_mqtt_client_start(mqtt_client_) != ESP_OK) {
            ESP_LOGE(tag, "Could not start Blynk MQTT client");
            static_cast<void>(esp_mqtt_client_destroy(mqtt_client_));
            mqtt_client_ = nullptr;
            return;
        }
        ESP_LOGI(tag, "Blynk MQTT enabled on configured regional endpoint");
    }

    void stop_mqtt() noexcept
    {
        record_disconnect();
        if (mqtt_client_ != nullptr) {
            static_cast<void>(esp_mqtt_client_stop(mqtt_client_));
            static_cast<void>(esp_mqtt_client_destroy(mqtt_client_));
            mqtt_client_ = nullptr;
        }
    }

    void mqtt_event(
        const esp_mqtt_event_handle_t event,
        const std::int32_t event_id
    ) noexcept
    {
        if (event == nullptr) return;
        if (event_id == MQTT_EVENT_CONNECTED) {
            const auto message_id = esp_mqtt_client_subscribe_single(
                event->client, downlink_subscription, 0
            );
            if (message_id >= 0) {
                static_cast<void>(connection_boundary_.callback_connected());
            } else {
                record_disconnect();
            }
            return;
        }
        if (event_id == MQTT_EVENT_DISCONNECTED || event_id == MQTT_EVENT_ERROR) {
            record_disconnect();
            return;
        }
        if (event_id != MQTT_EVENT_DATA || event->topic == nullptr
            || event->data == nullptr || event->current_data_offset != 0
            || event->data_len != event->total_data_len) return;

        const std::string_view topic{
            event->topic, static_cast<std::size_t>(event->topic_len)
        };
        constexpr std::string_view prefix{downlink_prefix};
        if (!topic.starts_with(prefix)) return;
        const std::string_view datastream = topic.substr(prefix.size());
        const std::string_view payload{
            event->data, static_cast<std::size_t>(event->data_len)
        };
        const auto connection_generation =
            connection_boundary_.callback_connection_generation();
        if (connection_boundary_.accepts(connection_generation)) {
            static_cast<void>(inbound_.push(
                datastream, payload, connection_generation
            ));
        }
    }

    void record_disconnect() noexcept
    {
        disconnect_inbound_drops_.store(
            inbound_.dropped_count(), std::memory_order_relaxed
        );
        connection_boundary_.callback_disconnected();
    }

    void run() noexcept
    {
        ESP_LOGI(
            tag,
            "BlynkTask started on core %d priority=%lu; TWDT subscription disabled",
            xPortGetCoreID(), static_cast<unsigned long>(uxTaskPriorityGet(nullptr))
        );
        start_mqtt();
        std::int64_t next_snapshot_ms = 0;
        while (running_.load(std::memory_order_acquire)) {
            service_uart();
            const auto connection = connection_boundary_.poll();
            if (connection.cleanup_required) handle_disconnect();
            if (connection.connection_started) projection_.connected();

            if (connection_boundary_.usable(connection)) {
                process_inbound(connection);
            }
            const auto now = monotonic_ms();
            if (now >= next_snapshot_ms) {
                observe_snapshot(connection_boundary_.usable(connection));
                next_snapshot_ms = now + snapshot_period_ms;
            }
            if (connection_boundary_.usable(connection)) {
                publish_outbound(now, connection);
            }
            vTaskDelay(task_poll_ticks);
        }
        stop_mqtt();
        task_.store(nullptr, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    void handle_disconnect() noexcept
    {
        projection_.disconnected();
        results_.disconnected();
        events_.disconnected();
        pending_feedback_.reset();
        observed_inbound_drops_ = disconnect_inbound_drops_.load(
            std::memory_order_acquire
        );
    }

    void service_uart() noexcept
    {
        std::array<std::uint8_t, 128U> bytes{};
        const int read = uart_read_bytes(
            UART_NUM_0, bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0
        );
        for (int index = 0; index < read; ++index) {
            const auto request = provisioning_parser_.consume(
                bytes[static_cast<std::size_t>(index)]
            );
            if (request) handle_provisioning(*request);
            const auto error = provisioning_parser_.take_error();
            if (error != BlynkProvisioningParseError::None) {
                write_uart_response("FUMURI-BLYNK/1 ERROR invalid_frame\n");
            }
        }
    }

    void handle_provisioning(const BlynkProvisioningRequest& request) noexcept
    {
        if (request.operation == BlynkProvisioningOperation::Status) {
            std::array<char, 256U> response{};
            if (configured_) {
                static_cast<void>(std::snprintf(
                    response.data(), response.size(),
                    "FUMURI-BLYNK/1 OK STATUS configured endpoint=%s template=%s token=present\n",
                    configuration_.endpoint.data(), configuration_.template_id.data()
                ));
            } else {
                static_cast<void>(std::snprintf(
                    response.data(), response.size(),
                    "FUMURI-BLYNK/1 OK STATUS disabled token=absent\n"
                ));
            }
            write_uart_response(response.data());
            return;
        }
        if (request.operation == BlynkProvisioningOperation::Clear) {
            if (!clear_configuration()) {
                write_uart_response("FUMURI-BLYNK/1 ERROR nvs_write_failed\n");
                return;
            }
            stop_mqtt();
            configuration_ = {};
            configured_ = false;
            handle_disconnect();
            write_uart_response("FUMURI-BLYNK/1 OK CLEAR disabled\n");
            return;
        }
        if (!persist_configuration(request.configuration)) {
            write_uart_response("FUMURI-BLYNK/1 ERROR nvs_write_failed\n");
            return;
        }
        stop_mqtt();
        configuration_ = request.configuration;
        configured_ = true;
        handle_disconnect();
        start_mqtt();
        write_uart_response("FUMURI-BLYNK/1 OK SET token=stored\n");
    }

    void write_uart_response(const std::string_view response) noexcept
    {
        static_cast<void>(uart_write_bytes(
            UART_NUM_0, response.data(), static_cast<std::size_t>(response.size())
        ));
    }

    void process_inbound(const BlynkConnectionSnapshot& connection) noexcept
    {
        const auto dropped = inbound_.dropped_count();
        if (dropped != observed_inbound_drops_) {
            observed_inbound_drops_ = dropped;
            events_.queue(BlynkEventType::RemoteError, "remote command mailbox saturated");
        }

        BlynkInboundCommand inbound;
        while (connection_boundary_.usable(connection)) {
            const auto front_generation = inbound_.front_connection_generation();
            if (!front_generation) break;
            if (*front_generation != connection.connection_generation
                && connection_boundary_.accepts(*front_generation)) {
                // A newer connection became active during this poll. Leave its
                // first command for the next poll, which must run cleanup first.
                break;
            }
            if (!inbound_.try_pop(inbound)) break;
            if (inbound.connection_generation != connection.connection_generation) {
                continue;
            }
            if (!connection_boundary_.usable(connection)) break;
            // Parsing remains single-sourced in BlynkCommandMapper. A malformed
            // atomic request or ignored `0` release can therefore leave an
            // intentional gap in the internal session-ID sequence without
            // admitting any command or correlation.
            const bool start = inbound.datastream_view() == "CmdStartRequest";
            const auto session_id = start ? ids_.next_session() : 1U;
            auto mapped = mapper_.map(
                inbound.datastream_view(), inbound.payload_view(), session_id
            );
            if (!connection_boundary_.usable(connection)) break;
            const auto protocol_error = blynk_command_error_message(mapped.decision);
            if (!protocol_error.empty()) {
                events_.queue(BlynkEventType::RemoteError, protocol_error);
                continue;
            }
            if (mapped.decision != BlynkCommandDecision::Accepted) continue;
            if (mapped.command) {
                const auto correlation = ids_.next_correlation();
                const auto admission = application_mailbox_.push(
                    std::move(*mapped.command), correlation,
                    connection.connection_generation
                );
                if (admission == app::MailboxAdmission::Accepted) {
                    if (!results_.track(correlation)) {
                        events_.queue(BlynkEventType::RemoteError, "remote result capacity exhausted");
                    }
                } else {
                    static_cast<void>(results_.record_service_result(correlation, false));
                    events_.queue(BlynkEventType::RemoteError, "remote application mailbox saturated");
                }
                continue;
            }
            if (mapped.firmware_operation == BlynkFirmwareOperation::None) continue;
            const auto correlation = ids_.next_correlation();
            if (mapped.firmware_operation == BlynkFirmwareOperation::Check) {
                static_cast<void>(results_.record_service_result(
                    correlation, firmware_updates_.request_check()
                ));
                continue;
            }
            const auto status = firmware_updates_.status();
            const auto admission = status.available_version[0] == '\0'
                ? FirmwareInstallAdmission::BusyOrUnavailable
                : firmware_updates_.request_install(
                    status.available_version.data(), correlation
                );
            if (admission == FirmwareInstallAdmission::Accepted) {
                if (!results_.track(correlation)) {
                    events_.queue(BlynkEventType::RemoteError, "remote result capacity exhausted");
                }
            } else {
                static_cast<void>(results_.record_service_result(correlation, false));
            }
        }
    }

    void observe_snapshot(const bool connected) noexcept
    {
        auto lease = snapshots_.acquire();
        if (!lease) return;
        const auto snapshot = lease.view();
        const auto firmware = firmware_updates_.status();
        projection_.observe(make_blynk_remote_status(snapshot, firmware));
        results_.observe(snapshot.command_results);

        if (connected && snapshot.active_fault
            && (!previous_fault_ || previous_fault_->code != snapshot.active_fault->code)) {
            events_.queue(BlynkEventType::Fault, "smoker fault active");
        }
        if (connected) {
            for (const auto& alarm : snapshot.active_alarms) {
                const auto end = previous_alarm_ids_.begin() + previous_alarm_count_;
                if (std::find(previous_alarm_ids_.begin(), end, alarm.id) == end) {
                    events_.queue(BlynkEventType::Alarm, "smoker alarm active");
                }
            }
            if (snapshot.session_id && terminal_status(snapshot.session_status)
                && (!previous_session_id_ || previous_session_id_ != snapshot.session_id
                    || !terminal_status(previous_session_status_))) {
                events_.queue(BlynkEventType::SessionDone, "smoker session finished");
            }
            if (firmware.state != previous_firmware_state_
                && ota_result_state(firmware.state)) {
                events_.queue(
                    BlynkEventType::Ota, firmware_update_state_name(firmware.state)
                );
            }
        }

        previous_fault_ = snapshot.active_fault;
        previous_session_id_ = snapshot.session_id;
        previous_session_status_ = snapshot.session_status;
        previous_firmware_state_ = firmware.state;
        previous_alarm_count_ = std::min(
            snapshot.active_alarms.size(), previous_alarm_ids_.size()
        );
        for (std::size_t index = 0U; index < previous_alarm_count_; ++index) {
            previous_alarm_ids_[index] = snapshot.active_alarms[index].id;
        }
    }

    void publish_outbound(
        const std::int64_t now,
        const BlynkConnectionSnapshot& connection
    ) noexcept
    {
        if (!pending_feedback_) pending_feedback_ = results_.pop();
        if (pending_feedback_) {
            const auto payload = serialize_blynk_command_feedback(*pending_feedback_);
            if (publish(result_topic, payload.view(), connection)) {
                pending_feedback_.reset();
            }
        }

        const auto event = events_.pending_publish(now);
        if (event) {
            std::array<char, 64U> topic{};
            const auto code = blynk_event_code(event->type);
            const auto written = std::snprintf(
                topic.data(), topic.size(), "event/%.*s",
                static_cast<int>(code.size()), code.data()
            );
            if (written > 0 && static_cast<std::size_t>(written) < topic.size()
                && publish(topic.data(), event->description.data(), connection)) {
                events_.publish_succeeded(event->type, now);
            }
        }

        const auto status = projection_.pending_publish(now);
        if (status) {
            const auto payload = serialize_blynk_batch(*status);
            if (payload && publish(status_topic, payload->view(), connection)) {
                projection_.publish_succeeded(now);
            }
        }
    }

    [[nodiscard]] bool publish(
        const std::string_view topic,
        const std::string_view payload,
        const BlynkConnectionSnapshot& connection
    ) noexcept
    {
        if (mqtt_client_ == nullptr || !connection_boundary_.usable(connection)
            || topic.empty() || payload.size() > blynk_payload_capacity) return false;
        return esp_mqtt_client_publish(
            mqtt_client_, topic.data(), payload.data(),
            static_cast<int>(payload.size()), 0, 0
        ) >= 0;
    }

    app::SpscCommandMailbox& application_mailbox_;
    const app::SnapshotExchange& snapshots_;
    FirmwareUpdateService& firmware_updates_;
    RuntimeIdGenerator& ids_;
    BlynkCommandMapper mapper_;
    BlynkInboundMailbox inbound_;
    BlynkRemoteProjection projection_;
    BlynkCommandResults results_;
    BlynkEventScheduler events_;
    BlynkProvisioningParser provisioning_parser_;
    BlynkProvisionedConfiguration configuration_{};
    BlynkConnectionBoundary connection_boundary_;
    std::optional<BlynkCommandFeedback> pending_feedback_;
    std::optional<core::Fault> previous_fault_;
    std::optional<core::SessionId> previous_session_id_;
    core::SessionStatus previous_session_status_{core::SessionStatus::Idle};
    FirmwareUpdateState previous_firmware_state_{FirmwareUpdateState::Idle};
    std::array<core::AlarmId, 16U> previous_alarm_ids_{};
    std::size_t previous_alarm_count_{0U};
    std::uint32_t observed_inbound_drops_{0U};
    std::atomic<std::uint32_t> disconnect_inbound_drops_{0U};
    nvs_handle_t nvs_handle_{0U};
    esp_mqtt_client_handle_t mqtt_client_{nullptr};
    std::atomic_bool running_{false};
    std::atomic<TaskHandle_t> task_{nullptr};
    bool configured_{false};
};

BlynkService::BlynkService(
    app::SpscCommandMailbox& application_mailbox,
    const app::SnapshotExchange& snapshots,
    FirmwareUpdateService& firmware_updates,
    RuntimeIdGenerator& ids,
    core::Recipe startup_recipe
) noexcept
    : impl_{new (std::nothrow) Impl{
          application_mailbox, snapshots, firmware_updates, ids,
          std::move(startup_recipe)
      }}
{
}

BlynkService::~BlynkService() = default;

bool BlynkService::start() noexcept
{
    return impl_ != nullptr && impl_->start();
}

bool BlynkService::accepts_connection_generation(
    const std::uint32_t connection_generation
) const noexcept
{
    return impl_ != nullptr
        && impl_->accepts_connection_generation(connection_generation);
}

} // namespace smoker::platform

#include "smoker/platform/simulation_runtime.hpp"

#include "smoker/app/command_mailbox.hpp"
#include "smoker/app/smoker_application.hpp"
#include "smoker/app/snapshot_exchange.hpp"
#include "smoker/platform/local_connectivity.hpp"
#include "smoker/platform/blynk_service.hpp"
#include "smoker/platform/firmware_update_service.hpp"
#include "smoker/platform/flash_operation_coordinator.hpp"
#include "smoker/platform/history_service.hpp"
#include "smoker/platform/history_support.hpp"
#include "smoker/platform/simulated_adapters.hpp"
#include "smoker/platform/runtime_transport_support.hpp"

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <chrono>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <variant>

namespace smoker::platform {
namespace {

using namespace std::chrono_literals;
constexpr char tag[] = "smoker_v0";
constexpr std::size_t control_task_stack_size_bytes = 12U * 1024U;
constexpr UBaseType_t control_task_priority = tskIDLE_PRIORITY + 2U;
constexpr std::size_t runtime_log_period_cycles = 60U;

static_assert(control_task_stack_size_bytes % sizeof(StackType_t) == 0U);

StaticTask_t control_task_buffer;
std::array<StackType_t, control_task_stack_size_bytes / sizeof(StackType_t)>
    control_task_stack;

class SimulationContext final {
public:
    explicit SimulationContext(SimulationRuntimeConfiguration configuration)
        : ambient{configuration.ambient_temperature}
        , probes{std::move(configuration.food_probe)}
        , chamber{ambient}
        , food_source{probes}
        , application{
              chamber,
              food_source,
              heater,
              clock,
              events,
              configuration.safety_limits,
              probes,
          }
        , ids{}
        , http_mailbox{}
        , blynk_mailbox{}
        , history_mailbox{probes.size(), (probes.size() * 2U) + 1U}
        , snapshots{probes.size(), (probes.size() * 2U) + 1U}
        , firmware_updates{snapshots, flash_operations}
        , history{history_mailbox, flash_operations}
        , connectivity{
              http_mailbox,
              snapshots,
              firmware_updates,
              history,
              ids,
              configuration.startup_recipe,
          }
        , blynk{
              blynk_mailbox,
              snapshots,
              firmware_updates,
              ids,
              std::move(configuration.startup_recipe),
          }
    {
        static_cast<void>(food_source.set_reading(probes.front().id, ambient));
    }

    core::Temperature ambient;
    std::array<core::FoodProbeConfig, 1U> probes;
    SimulatedChamberSensor chamber;
    SimulatedFoodProbeSource food_source;
    SimulatedHeaterOutput heater;
    SimulatedClock clock;
    SimulatedEventSink events;
    app::SmokerApplication application;
    RuntimeIdGenerator ids;
    app::SpscCommandMailbox http_mailbox;
    app::SpscCommandMailbox blynk_mailbox;
    FlashOperationCoordinator flash_operations;
    HistoryObservationMailbox history_mailbox;
    app::SnapshotExchange snapshots;
    FirmwareUpdateService firmware_updates;
    HistoryService history;
    LocalConnectivityService connectivity;
    BlynkService blynk;
};

[[nodiscard]] bool subscribe_control_loop_to_watchdog() noexcept
{
    const auto status = esp_task_wdt_status(nullptr);
    if (status == ESP_OK) {
        return true;
    }
    if (status != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(tag, "Task watchdog is unavailable: %s", esp_err_to_name(status));
        return false;
    }
    return esp_task_wdt_add(nullptr) == ESP_OK;
}

void log_target_inventory() noexcept
{
    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);
    ESP_LOGI(
        tag,
        "target model=%u revision=%u.%u cores=%u Wi-Fi=%s BLE=%s reset_reason=%d",
        static_cast<unsigned>(chip_info.model),
        static_cast<unsigned>(chip_info.revision / 100U),
        static_cast<unsigned>(chip_info.revision % 100U),
        static_cast<unsigned>(chip_info.cores),
        (chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0U ? "yes" : "no",
        (chip_info.features & CHIP_FEATURE_BLE) != 0U ? "yes" : "no",
        static_cast<int>(esp_reset_reason())
    );

    std::uint32_t flash_size_bytes = 0U;
    const auto flash_status = esp_flash_get_size(nullptr, &flash_size_bytes);
    if (flash_status == ESP_OK) {
        ESP_LOGI(
            tag,
            "target flash=%lu MiB PSRAM=%zu MiB initialized=%s",
            static_cast<unsigned long>(flash_size_bytes / (1024U * 1024U)),
            esp_psram_get_size() / (1024U * 1024U),
            esp_psram_is_initialized() ? "yes" : "no"
        );
    } else {
        ESP_LOGE(tag, "Could not read target flash size: %s", esp_err_to_name(flash_status));
    }
}

[[noreturn]] void suspend_control_task() noexcept
{
    while (true) {
        vTaskSuspend(nullptr);
    }
}

void control_task(void* const parameter)
{
    std::unique_ptr<SimulationContext> context{static_cast<SimulationContext*>(parameter)};

    if (!subscribe_control_loop_to_watchdog()) {
        ESP_LOGE(tag, "Control-loop watchdog subscription failed; heater remains OFF");
        suspend_control_task();
    }

    constexpr auto control_period = 1s;
    constexpr auto control_period_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(control_period).count();
    constexpr auto control_period_ticks = pdMS_TO_TICKS(control_period_milliseconds);
    static_assert(control_period_ticks > 0U);
    TickType_t last_wake = xTaskGetTickCount();
    float last_logged_demand = -1.0F;
    std::optional<float> last_logged_target;
    bool runtime_state_logged = false;
    std::size_t completed_cycles = 0U;
    RoundRobinCommandDrain external_commands;
    const auto submit_to_application = [](void* const application_context,
                                          app::Command command_to_submit,
                                          const std::uint32_t command_correlation_id) noexcept {
        return static_cast<app::SmokerApplication*>(application_context)->submit(
            std::move(command_to_submit), command_correlation_id
        );
    };
    while (true) {
        std::uint32_t internal_correlation_id = 0U;
        bool prepare_submission_pending = false;
        if (context->firmware_updates.consume_prepare_request(
                internal_correlation_id
            )
            && !submit_to_application(
                &context->application,
                app::PrepareFirmwareUpdateCommand{}, internal_correlation_id
            )) {
            context->firmware_updates.retry_prepare_request(
                internal_correlation_id
            );
            prepare_submission_pending = true;
        }
        // A timeout may publish Finish while Prepare is still waiting for an
        // application-queue slot. Never let Finish overtake that reservation:
        // once Prepare is admitted, the application FIFO preserves their order.
        if (!prepare_submission_pending
            && context->firmware_updates.consume_finish_request()
            && !submit_to_application(
                &context->application, app::FinishFirmwareUpdateCommand{}, 0U
            )) {
            context->firmware_updates.retry_finish_request();
        }
        static_cast<void>(external_commands.drain(
            context->http_mailbox,
            context->blynk_mailbox,
            &context->application,
            submit_to_application
        ));
        context->application.tick();
        const auto snapshot = context->application.snapshot_view();
        static_cast<void>(context->snapshots.publish(snapshot));
        context->history_mailbox.observe(snapshot);
        ++completed_cycles;
        if (completed_cycles == 1U || completed_cycles % runtime_log_period_cycles == 0U) {
            const auto minimum_free_stack = uxTaskGetStackHighWaterMark2(nullptr);
            ESP_LOGI(
                tag,
                "ControlTask cycle=%zu core=%d priority=%lu stack=%lu/%zu bytes minimum free/allocated",
                completed_cycles,
                static_cast<int>(xPortGetCoreID()),
                static_cast<unsigned long>(uxTaskPriorityGet(nullptr)),
                static_cast<unsigned long>(minimum_free_stack),
                control_task_stack_size_bytes
            );
        }

        const float current_demand = snapshot.heater_demand.percent();
        const auto current_target = snapshot.chamber_target
            ? std::optional<float>{snapshot.chamber_target->celsius()}
            : std::nullopt;
        if (!runtime_state_logged || current_demand != last_logged_demand
            || current_target != last_logged_target) {
            runtime_state_logged = true;
            last_logged_demand = current_demand;
            last_logged_target = current_target;
            if (snapshot.chamber_temperature && snapshot.chamber_target) {
                ESP_LOGI(
                    tag,
                    "simulation chamber=%.1f C target=%.1f C heater=%.1f%%",
                    static_cast<double>(snapshot.chamber_temperature->celsius()),
                    static_cast<double>(snapshot.chamber_target->celsius()),
                    static_cast<double>(current_demand)
                );
            } else if (snapshot.chamber_temperature) {
                ESP_LOGI(
                    tag,
                    "simulation chamber=%.1f C target=none heater=%.1f%%",
                    static_cast<double>(snapshot.chamber_temperature->celsius()),
                    static_cast<double>(current_demand)
                );
            } else if (snapshot.chamber_target) {
                ESP_LOGI(
                    tag,
                    "simulation chamber=unavailable target=%.1f C heater=%.1f%%",
                    static_cast<double>(snapshot.chamber_target->celsius()),
                    static_cast<double>(current_demand)
                );
            } else {
                ESP_LOGI(
                    tag,
                    "simulation chamber=unavailable target=none heater=%.1f%%",
                    static_cast<double>(current_demand)
                );
            }
        }

        const auto watchdog_status = esp_task_wdt_reset();
        context->firmware_updates.publish_control_cycle(
            snapshot, watchdog_status == ESP_OK
        );
        ESP_ERROR_CHECK(watchdog_status);
        xTaskDelayUntil(&last_wake, control_period_ticks);
        context->clock.advance(control_period);
    }
}

} // namespace

bool start_simulation_runtime(SimulationRuntimeConfiguration configuration) noexcept
{
    log_target_inventory();

    auto context = std::unique_ptr<SimulationContext>{
        new (std::nothrow) SimulationContext{std::move(configuration)}
    };
    if (!context) {
        ESP_LOGE(tag, "Could not allocate simulation context; heater remains OFF");
        rollback_pending_firmware_and_reboot_if_needed();
        return false;
    }

    auto* const task_context = context.release();
    const auto task = xTaskCreateStaticPinnedToCore(
        control_task,
        "ControlTask",
        static_cast<std::uint32_t>(control_task_stack_size_bytes),
        task_context,
        control_task_priority,
        control_task_stack.data(),
        &control_task_buffer,
        1
    );
    if (task == nullptr) {
        context.reset(task_context);
        ESP_LOGE(tag, "Could not create ControlTask; heater remains OFF");
        rollback_pending_firmware_and_reboot_if_needed();
        return false;
    }

    if (!task_context->firmware_updates.start()) {
        ESP_LOGE(tag, "OTA service unavailable; autonomous ControlTask continues");
    }
    if (!task_context->history.start()) {
        ESP_LOGE(tag, "History service unavailable; autonomous ControlTask continues");
    }
    if (!task_context->connectivity.start()) {
        ESP_LOGE(tag, "Local connectivity unavailable; autonomous ControlTask continues");
    }
    if (!task_context->blynk.start()) {
        ESP_LOGE(tag, "Blynk service unavailable; autonomous ControlTask continues");
    }
    task_context->connectivity.mark_control_ready();

    return true;
}

} // namespace smoker::platform

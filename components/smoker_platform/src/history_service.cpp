#include "smoker/platform/history_service.hpp"
#include "smoker/platform/wall_clock.hpp"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace smoker::platform {
namespace {

constexpr char tag[] = "smoker_history";
constexpr char history_partition_label[] = "history";
constexpr std::uint8_t history_partition_subtype = 0x40U;
constexpr std::size_t history_task_stack_size_bytes = 12U * 1024U;
constexpr UBaseType_t history_task_priority = tskIDLE_PRIORITY + 1U;
static_assert(history_task_stack_size_bytes % sizeof(StackType_t) == 0U);

// HistoryTask calls raw SPI-flash APIs. Keep its static storage in internal
// DRAM while the cache and PSRAM may be unavailable.
DRAM_ATTR StaticTask_t history_task_storage;
DRAM_ATTR std::array<StackType_t, history_task_stack_size_bytes / sizeof(StackType_t)>
    history_task_stack;

class PartitionHistoryFlash final : public IHistoryFlash {
public:
    explicit PartitionHistoryFlash(const esp_partition_t* const partition) noexcept
        : partition_{partition}
    {
    }

    std::size_t size() const noexcept override
    {
        return partition_ == nullptr ? 0U : partition_->size;
    }
    std::size_t sector_size() const noexcept override { return history_page_bytes; }
    bool read(
        const std::size_t offset, const std::span<std::uint8_t> destination
    ) const noexcept override
    {
        return partition_ != nullptr
            && esp_partition_read(partition_, offset, destination.data(), destination.size())
                == ESP_OK;
    }
    bool write(
        const std::size_t offset, const std::span<const std::uint8_t> source
    ) noexcept override
    {
        const bool success = partition_ != nullptr
            && esp_partition_write(partition_, offset, source.data(), source.size())
                == ESP_OK;
        if (success) ++write_operations_;
        return success;
    }
    bool erase_sector(const std::size_t offset) noexcept override
    {
        const bool success = partition_ != nullptr
            && esp_partition_erase_range(partition_, offset, history_page_bytes) == ESP_OK;
        if (success) ++write_operations_;
        return success;
    }
    [[nodiscard]] std::uint64_t write_operations() const noexcept
    {
        return write_operations_;
    }

private:
    const esp_partition_t* partition_{nullptr};
    std::uint64_t write_operations_{0U};
};

} // namespace

class HistoryService::Impl final {
public:
    Impl(
        HistoryObservationMailbox& mailbox,
        FlashOperationCoordinator& flash_operations
    ) noexcept
        : mailbox_{mailbox}
        , flash_operations_{flash_operations}
        , partition_{esp_partition_find_first(
              ESP_PARTITION_TYPE_DATA,
              static_cast<esp_partition_subtype_t>(history_partition_subtype),
              history_partition_label
          )}
        , flash_{partition_}
        , log_{flash_}
    {
    }

    ~Impl()
    {
        running_.store(false, std::memory_order_release);
        const auto task = task_.load(std::memory_order_acquire);
        if (task != nullptr) xTaskNotifyGive(task);
    }

    bool start() noexcept
    {
        if (partition_ == nullptr || partition_->size != history_partition_bytes) {
            failed_.store(true, std::memory_order_release);
            ESP_LOGE(tag, "History partition missing or has unexpected size");
            return false;
        }
        running_.store(true, std::memory_order_release);
        const auto task = xTaskCreateStaticPinnedToCore(
            &Impl::task_entry,
            "HistoryTask",
            static_cast<std::uint32_t>(history_task_stack_size_bytes),
            this,
            history_task_priority,
            history_task_stack.data(),
            &history_task_storage,
            0
        );
        if (task == nullptr) {
            running_.store(false, std::memory_order_release);
            failed_.store(true, std::memory_order_release);
            ESP_LOGE(tag, "Could not create static HistoryTask");
            return false;
        }
        task_.store(task, std::memory_order_release);
        return true;
    }

    HistoryHealth health() const noexcept
    {
        if (failed_.load(std::memory_order_acquire)
            && !initialized_.load(std::memory_order_acquire)) {
            return HistoryHealth{
                HistoryStorageState::Failed,
                mailbox_.dropped_count(), 0U, 1U,
                partition_ == nullptr ? 0U : partition_->size, 0U
            };
        }
        std::lock_guard lock{mutex_};
        auto result = log_.health();
        result.mailbox_drops = mailbox_.dropped_count();
        if (failed_.load(std::memory_order_acquire)) {
            result.state = HistoryStorageState::Failed;
        }
        if (result.mailbox_drops != 0U && result.state == HistoryStorageState::Ready) {
            result.state = HistoryStorageState::Degraded;
        }
        return result;
    }

    HistoryQueryResult sessions(
        const std::optional<std::uint64_t> before,
        const std::size_t limit,
        std::vector<HistorySessionSummary>& destination
    ) const
    {
        if (failed_.load(std::memory_order_acquire)
            || !initialized_.load(std::memory_order_acquire)) {
            return HistoryQueryResult::Failed;
        }
        HistoryFlashLease flash_lease{flash_operations_};
        if (!flash_lease) return HistoryQueryResult::Busy;
        std::lock_guard lock{mutex_};
        destination = log_.sessions(before, limit);
        return log_.last_query_failed()
            ? HistoryQueryResult::Failed : HistoryQueryResult::Ok;
    }

    HistoryQueryResult samples(
        const std::uint64_t history_id,
        const std::optional<std::uint32_t> after,
        const std::size_t limit,
        const std::uint16_t stride,
        std::vector<HistoryObservation>& destination,
        std::optional<std::uint32_t>& continuation
    ) const
    {
        if (failed_.load(std::memory_order_acquire)
            || !initialized_.load(std::memory_order_acquire)) {
            return HistoryQueryResult::Failed;
        }
        HistoryFlashLease flash_lease{flash_operations_};
        if (!flash_lease) return HistoryQueryResult::Busy;
        std::lock_guard lock{mutex_};
        if (!log_.contains_session(history_id)) return HistoryQueryResult::NotFound;
        destination = log_.samples(history_id, after, limit, stride, continuation);
        return log_.last_query_failed()
            ? HistoryQueryResult::Failed : HistoryQueryResult::Ok;
    }

private:
    static void task_entry(void* const parameter) noexcept
    {
        static_cast<Impl*>(parameter)->run();
    }

    void run() noexcept
    {
        ESP_LOGI(
            tag, "HistoryTask started on core %d; TWDT subscription disabled",
            xPortGetCoreID()
        );
        while (running_.load(std::memory_order_acquire)
            && !initialized_.load(std::memory_order_acquire)) {
            HistoryFlashLease flash_lease{flash_operations_};
            if (flash_lease) {
                std::lock_guard lock{mutex_};
                const bool ready = log_.initialize();
                if (!ready && log_.health().state == HistoryStorageState::Failed) {
                    failed_.store(true, std::memory_order_release);
                }
                initialized_.store(ready, std::memory_order_release);
                if (initialized_.load(std::memory_order_acquire)) {
                    const auto health = log_.health();
                    ESP_LOGI(
                        tag, "History ready capacity=%zu used=%zu status=%u",
                        health.capacity_bytes, health.used_bytes,
                        static_cast<unsigned>(health.state)
                    );
                    break;
                }
            }
            if (failed_.load(std::memory_order_acquire)) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (failed_.load(std::memory_order_acquire)
            || !initialized_.load(std::memory_order_acquire)) {
            task_.store(nullptr, std::memory_order_release);
            vTaskDelete(nullptr);
            return;
        }

        HistoryObservation observation;
        HistoryWritePolicy write_policy;
        while (running_.load(std::memory_order_acquire)) {
            bool terminal_failure = false;
            {
                HistoryFlashLease flash_lease{flash_operations_};
                std::optional<HistoryObservation> incoming;
                if (flash_lease && !write_policy.has_pending_lifecycle()
                    && mailbox_.try_pop(observation)) {
                    if (!observation.unix_utc_seconds) {
                        observation.unix_utc_seconds = synchronized_unix_utc_now();
                    }
                    incoming = observation;
                }
                if (flash_lease
                    && (write_policy.has_pending_lifecycle() || incoming)) {
                    std::lock_guard lock{mutex_};
                    log_.set_mailbox_drops(mailbox_.dropped_count());
                    const auto result = write_policy.process(
                        std::move(incoming),
                        [this](const HistoryObservation& candidate) {
                            const bool written = candidate.kind
                                    == HistoryObservationKind::Start
                                ? log_.begin_session(candidate).has_value()
                                : log_.append(candidate);
                            return HistoryWriteAttemptResult{
                                written, log_.health().state
                            };
                        }
                    );
                    if (result == HistoryWriteCycleResult::Written) {
                        ++records_written_;
                        if (records_written_ == 1U || records_written_ % 60U == 0U) {
                            ESP_LOGI(
                                tag,
                                "HistoryTask records=%llu flash_ops=%llu core=%d stack=%lu/%zu bytes minimum free/allocated",
                                static_cast<unsigned long long>(records_written_),
                                static_cast<unsigned long long>(flash_.write_operations()),
                                xPortGetCoreID(),
                                static_cast<unsigned long>(uxTaskGetStackHighWaterMark2(nullptr)),
                                history_task_stack_size_bytes
                            );
                        }
                    } else if (result == HistoryWriteCycleResult::TerminalFailStop) {
                        failed_.store(true, std::memory_order_release);
                        terminal_failure = true;
                    }
                }
            }
            if (terminal_failure) {
                ESP_LOGE(tag, "History storage FAILED; stopping flash persistence");
                break;
            }
            static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)));
        }
        task_.store(nullptr, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    HistoryObservationMailbox& mailbox_;
    FlashOperationCoordinator& flash_operations_;
    const esp_partition_t* partition_{nullptr};
    PartitionHistoryFlash flash_;
    mutable CircularHistoryLog log_;
    mutable std::mutex mutex_;
    std::atomic_bool running_{false};
    std::atomic_bool initialized_{false};
    std::atomic_bool failed_{false};
    std::atomic<TaskHandle_t> task_{nullptr};
    std::uint64_t records_written_{0U};
};

HistoryService::HistoryService(
    HistoryObservationMailbox& mailbox,
    FlashOperationCoordinator& flash_operations
) noexcept
    : impl_{new (std::nothrow) Impl{mailbox, flash_operations}}
{
}

HistoryService::~HistoryService() = default;

bool HistoryService::start() noexcept
{
    return impl_ != nullptr && impl_->start();
}

HistoryHealth HistoryService::health() const noexcept
{
    return impl_ != nullptr ? impl_->health()
        : HistoryHealth{HistoryStorageState::Failed, 0U, 0U, 1U, 0U, 0U};
}

HistoryQueryResult HistoryService::sessions(
    const std::optional<std::uint64_t> before,
    const std::size_t limit,
    std::vector<HistorySessionSummary>& destination
) const
{
    return impl_ != nullptr
        ? impl_->sessions(before, limit, destination)
        : HistoryQueryResult::Failed;
}

HistoryQueryResult HistoryService::samples(
    const std::uint64_t history_id,
    const std::optional<std::uint32_t> after,
    const std::size_t limit,
    const std::uint16_t stride,
    std::vector<HistoryObservation>& destination,
    std::optional<std::uint32_t>& continuation
) const
{
    return impl_ != nullptr
        ? impl_->samples(history_id, after, limit, stride, destination, continuation)
        : HistoryQueryResult::Failed;
}

} // namespace smoker::platform

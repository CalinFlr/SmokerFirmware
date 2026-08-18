#pragma once

#include "smoker/app/snapshot_exchange.hpp"
#include "smoker/platform/firmware_update_support.hpp"
#include "smoker/platform/flash_operation_coordinator.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace smoker::platform {

enum class FirmwareInstallAdmission : std::uint8_t {
    Accepted,
    BusyOrUnavailable,
    Running,
    VersionMismatch,
};

[[nodiscard]] bool running_firmware_validation_pending() noexcept;

// Critical bootstrap can fail before FirmwareUpdateService exists. This helper
// keeps those failures inside the same rollback policy; it returns only when
// the running image is not pending verification.
void rollback_pending_firmware_and_reboot_if_needed() noexcept;

class FirmwareUpdateService final {
public:
    FirmwareUpdateService(
        const app::SnapshotExchange& snapshots,
        FlashOperationCoordinator& flash_operations
    ) noexcept;
    ~FirmwareUpdateService();

    FirmwareUpdateService(const FirmwareUpdateService&) = delete;
    FirmwareUpdateService& operator=(const FirmwareUpdateService&) = delete;

    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] FirmwareUpdateStatus status() const noexcept;
    [[nodiscard]] bool request_check() noexcept;
    [[nodiscard]] FirmwareInstallAdmission request_install(
        std::string_view version,
        std::uint32_t permission_correlation_id
    ) noexcept;

    // These are the only ControlTask-facing operations. They are bounded
    // atomic exchanges/stores and never perform network, clock, flash, mutex,
    // allocation, logging, or OTA API work.
    [[nodiscard]] bool consume_prepare_request(
        std::uint32_t& correlation_id
    ) noexcept;
    [[nodiscard]] bool consume_finish_request() noexcept;
    void retry_prepare_request(std::uint32_t correlation_id) noexcept;
    void retry_finish_request() noexcept;
    void publish_control_cycle(
        const app::SmokerSnapshotView& snapshot,
        bool watchdog_reset_succeeded
    ) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smoker::platform

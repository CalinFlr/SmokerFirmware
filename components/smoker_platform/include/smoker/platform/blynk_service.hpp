#pragma once

#include "smoker/app/command_mailbox.hpp"
#include "smoker/app/snapshot_exchange.hpp"
#include "smoker/core/domain.hpp"
#include "smoker/platform/firmware_update_service.hpp"
#include "smoker/platform/runtime_transport_support.hpp"

#include <memory>

namespace smoker::platform {

class BlynkService final {
public:
    BlynkService(
        app::SpscCommandMailbox& application_mailbox,
        const app::SnapshotExchange& snapshots,
        FirmwareUpdateService& firmware_updates,
        RuntimeIdGenerator& ids,
        core::Recipe startup_recipe
    ) noexcept;
    ~BlynkService();

    BlynkService(const BlynkService&) = delete;
    BlynkService& operator=(const BlynkService&) = delete;

    // Starts even without a credential blob so UART0 provisioning remains
    // available. Missing/invalid credentials disable only MQTT/Blynk.
    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] bool accepts_connection_generation(
        std::uint32_t connection_generation
    ) const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smoker::platform

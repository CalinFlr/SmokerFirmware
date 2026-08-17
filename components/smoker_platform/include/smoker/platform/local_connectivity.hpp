#pragma once

#include "smoker/app/command_mailbox.hpp"
#include "smoker/app/snapshot_exchange.hpp"
#include "smoker/core/domain.hpp"
#include "smoker/platform/firmware_update_service.hpp"

#include <memory>

namespace smoker::platform {

class LocalConnectivityService final {
public:
    LocalConnectivityService(
        app::SpscCommandMailbox& command_mailbox,
        const app::SnapshotExchange& snapshots,
        FirmwareUpdateService& firmware_updates,
        core::Recipe startup_recipe
    ) noexcept;
    ~LocalConnectivityService();

    LocalConnectivityService(const LocalConnectivityService&) = delete;
    LocalConnectivityService& operator=(const LocalConnectivityService&) = delete;

    [[nodiscard]] bool start() noexcept;
    void mark_control_ready() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smoker::platform

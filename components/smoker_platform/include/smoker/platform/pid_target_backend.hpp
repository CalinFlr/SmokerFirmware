#pragma once

#include "smoker/platform/pid_chamber_controller.hpp"

#include "pid_ctrl.h"

namespace smoker::platform {

// Target-only owner of the exact 0.3.1 float control block. Creation may
// allocate before ControlTask starts; valid compute/reset calls reuse it.
class EspressifPidFloatBackend final : public IPidControllerBackend {
public:
    EspressifPidFloatBackend() noexcept = default;
    ~EspressifPidFloatBackend() override;

    EspressifPidFloatBackend(const EspressifPidFloatBackend&) = delete;
    EspressifPidFloatBackend& operator=(const EspressifPidFloatBackend&) = delete;
    EspressifPidFloatBackend(EspressifPidFloatBackend&&) = delete;
    EspressifPidFloatBackend& operator=(EspressifPidFloatBackend&&) = delete;

    [[nodiscard]] bool initialize(
        const PidControllerConfiguration& configuration
    ) noexcept override;
    [[nodiscard]] bool compute(float error, float& output_percent) noexcept override;
    [[nodiscard]] bool reset() noexcept override;

private:
    void release() noexcept;

    pid_ctrl_block_handle_f_t handle_{nullptr};
};

} // namespace smoker::platform

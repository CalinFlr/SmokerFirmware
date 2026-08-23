#pragma once

#include "smoker/app/ports.hpp"

#include <cstdint>
#include <optional>

namespace smoker::platform {

enum class PidCalculationForm : std::uint8_t {
    Positional,
    Incremental,
};

// Only positional pid_ctrl accumulates raw per-call error and applies these
// bounds to that accumulated error before multiplying it by Ki.
struct PositionalAccumulatedErrorBounds final {
    float minimum;
    float maximum;
};

// Every common coefficient/bound and the calculation form are mandatory. The
// positional-only bounds must be present only for Positional; Incremental uses
// retained output saturation and must leave them absent. This inactive slice
// supplies no production defaults, gains, form, or cadence claim.
struct PidControllerConfiguration final {
    float proportional_gain;
    float integral_gain;
    float derivative_gain;
    float minimum_output_percent;
    float maximum_output_percent;
    PidCalculationForm calculation_form;
    std::optional<PositionalAccumulatedErrorBounds>
        positional_accumulated_error_bounds;
};

[[nodiscard]] bool valid_pid_controller_configuration(
    const PidControllerConfiguration& configuration
) noexcept;

// Platform-neutral seam used by host tests. The target implementation owns the
// exact pid_ctrl handle; ESP-IDF and component types do not cross this boundary.
class IPidControllerBackend {
public:
    virtual ~IPidControllerBackend() = default;

    [[nodiscard]] virtual bool initialize(
        const PidControllerConfiguration& configuration
    ) noexcept = 0;
    [[nodiscard]] virtual bool compute(float error, float& output_percent) noexcept = 0;
    [[nodiscard]] virtual bool reset() noexcept = 0;
};

class PidChamberController final : public app::IChamberController {
public:
    PidChamberController(
        PidControllerConfiguration configuration,
        IPidControllerBackend& backend
    ) noexcept;

    PidChamberController(const PidChamberController&) = delete;
    PidChamberController& operator=(const PidChamberController&) = delete;
    PidChamberController(PidChamberController&&) = delete;
    PidChamberController& operator=(PidChamberController&&) = delete;

    [[nodiscard]] std::optional<core::HeaterDemand> request(
        core::Temperature chamber_temperature,
        core::Temperature chamber_target
    ) noexcept override;
    [[nodiscard]] bool reset() noexcept override;
    [[nodiscard]] bool initialized() const noexcept;

private:
    PidControllerConfiguration configuration_;
    IPidControllerBackend& backend_;
    bool initialized_{false};
};

} // namespace smoker::platform

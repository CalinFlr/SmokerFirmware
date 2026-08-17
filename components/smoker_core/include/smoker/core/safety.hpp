#pragma once

#include "smoker/core/domain.hpp"

#include <optional>

namespace smoker::core {

struct SafetyEvaluation final {
    std::optional<FaultCode> fault_code;

    [[nodiscard]] constexpr bool heating_allowed() const noexcept
    {
        return !fault_code.has_value();
    }
};

[[nodiscard]] SafetyEvaluation evaluate_chamber_safety(
    const std::optional<Temperature>& chamber_temperature,
    const SafetyLimits& limits
) noexcept;

[[nodiscard]] HeaterDemand apply_safety_gate(
    HeaterDemand requested_demand,
    SessionStatus session_status,
    const std::optional<Fault>& active_fault
) noexcept;

} // namespace smoker::core

#include "smoker/core/safety.hpp"

namespace smoker::core {

SafetyEvaluation evaluate_chamber_safety(
    const std::optional<Temperature>& chamber_temperature,
    const SafetyLimits& limits
) noexcept
{
    if (!chamber_temperature) {
        return SafetyEvaluation{FaultCode::ChamberSensorInvalid};
    }

    if (*chamber_temperature > limits.maximum_chamber_temperature) {
        return SafetyEvaluation{FaultCode::ChamberOverTemperature};
    }

    return SafetyEvaluation{};
}

HeaterDemand apply_safety_gate(
    const HeaterDemand requested_demand,
    const SessionStatus session_status,
    const std::optional<Fault>& active_fault
) noexcept
{
    if (session_status != SessionStatus::Running || active_fault) {
        return HeaterDemand::off();
    }

    return requested_demand;
}

} // namespace smoker::core

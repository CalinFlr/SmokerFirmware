#include "smoker/platform/pid_target_backend.hpp"

#include "esp_err.h"

namespace smoker::platform {
namespace {

[[nodiscard]] pid_calculate_type_t driver_calculation_form(
    const PidCalculationForm form
) noexcept
{
    return form == PidCalculationForm::Positional
        ? PID_CAL_TYPE_POSITIONAL
        : PID_CAL_TYPE_INCREMENTAL;
}

} // namespace

EspressifPidFloatBackend::~EspressifPidFloatBackend()
{
    release();
}

bool EspressifPidFloatBackend::initialize(
    const PidControllerConfiguration& configuration
) noexcept
{
    release();
    if (!valid_pid_controller_configuration(configuration)) return false;

    // pid_ctrl 0.3.1 ignores these fields in incremental form. Keep the target
    // mapping deterministic without exposing an integral-bound promise to an
    // incremental project caller.
    const auto positional_bounds =
        configuration.positional_accumulated_error_bounds.value_or(
            PositionalAccumulatedErrorBounds{0.0F, 0.0F}
        );
    const pid_ctrl_config_f_t driver_configuration{
        .init_param = {
            .kp = configuration.proportional_gain,
            .ki = configuration.integral_gain,
            .kd = configuration.derivative_gain,
            .max_output = configuration.maximum_output_percent,
            .min_output = configuration.minimum_output_percent,
            .max_integral = positional_bounds.maximum,
            .min_integral = positional_bounds.minimum,
            .cal_type = driver_calculation_form(configuration.calculation_form),
        },
    };
    const auto result = pid_new_control_block_f(&driver_configuration, &handle_);
    if (result == ESP_OK && handle_ != nullptr) return true;

    release();
    return false;
}

bool EspressifPidFloatBackend::compute(
    const float error, float& output_percent
) noexcept
{
    return handle_ != nullptr
        && pid_compute_f(handle_, error, &output_percent) == ESP_OK;
}

bool EspressifPidFloatBackend::reset() noexcept
{
    return handle_ != nullptr && pid_reset_ctrl_block_f(handle_) == ESP_OK;
}

void EspressifPidFloatBackend::release() noexcept
{
    if (handle_ == nullptr) return;
    static_cast<void>(pid_del_control_block_f(handle_));
    handle_ = nullptr;
}

} // namespace smoker::platform

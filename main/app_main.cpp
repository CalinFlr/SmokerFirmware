#include "sdkconfig.h"

#ifdef CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC
#include "smoker/platform/max31865_connected_diagnostic.hpp"
#else
#include "smoker/platform/ordinary_runtime.hpp"
#endif

#include "esp_log.h"

#include <optional>
#include <utility>

namespace {

constexpr char tag[] = "smoker_v0";

} // namespace

extern "C" void app_main()
{
#ifdef CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC
    if (!smoker::platform::run_max31865_connected_diagnostic()) {
        ESP_LOGE(tag, "Connected sensor diagnostic failed; heater remains absent/OFF");
    }
#else
    using smoker::core::Temperature;

    const auto simulated_food_temperature = Temperature::from_celsius(25.0F);
    const auto chamber_target = Temperature::from_celsius(110.0F);
    const auto probe_target = Temperature::from_celsius(75.0F);
    const auto built_in_maximum = Temperature::from_celsius(150.0F);
    if (!simulated_food_temperature || !chamber_target || !probe_target
        || !built_in_maximum) {
        ESP_LOGE(tag, "Invalid built-in runtime configuration; heater remains OFF");
        return;
    }

    auto configuration = smoker::platform::OrdinaryRuntimeConfiguration{
        *simulated_food_temperature,
        smoker::core::FoodProbeConfig{
            1U,
            "Simulated food",
            smoker::core::ProbeRole::Meat,
            *probe_target,
            true,
            true,
        },
        smoker::core::SafetyLimits{*built_in_maximum},
        smoker::core::Recipe{
            1U,
            "ESP-IDF ordinary runtime",
            smoker::core::Stage{
                1U,
                "Single V0 stage",
                *chamber_target,
                std::nullopt,
            },
        },
    };
    if (!smoker::platform::start_ordinary_runtime(std::move(configuration))) {
        ESP_LOGE(tag, "Could not start ordinary runtime; heater remains OFF");
    }
#endif
}

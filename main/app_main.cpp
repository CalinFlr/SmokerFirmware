#include "smoker/platform/simulation_runtime.hpp"

#include "esp_log.h"

#include <optional>
#include <utility>

namespace {

constexpr char tag[] = "smoker_v0";

} // namespace

extern "C" void app_main()
{
    using smoker::core::Temperature;

    const auto ambient = Temperature::from_celsius(25.0F);
    const auto chamber_target = Temperature::from_celsius(110.0F);
    const auto probe_target = Temperature::from_celsius(75.0F);
    const auto simulated_maximum = Temperature::from_celsius(150.0F);
    if (!ambient || !chamber_target || !probe_target || !simulated_maximum) {
        ESP_LOGE(tag, "Invalid built-in simulation configuration; heater remains OFF");
        return;
    }

    auto configuration = smoker::platform::SimulationRuntimeConfiguration{
        *ambient,
        smoker::core::FoodProbeConfig{
            1U,
            "Simulated food",
            smoker::core::ProbeRole::Meat,
            *probe_target,
            true,
            true,
        },
        smoker::core::SafetyLimits{*simulated_maximum},
        smoker::core::Recipe{
            1U,
            "ESP-IDF simulation",
            smoker::core::Stage{
                1U,
                "Single simulated stage",
                *chamber_target,
                std::nullopt,
            },
        },
    };
    if (!smoker::platform::start_simulation_runtime(std::move(configuration))) {
        ESP_LOGE(tag, "Could not start simulation runtime; heater remains OFF");
    }
}

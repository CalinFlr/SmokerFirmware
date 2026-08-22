#pragma once

namespace smoker::platform {

// Target-only bring-up entrypoint. It owns only the diagnostic SPI bus/device,
// never constructs SmokerApplication, and never initializes heater output.
[[nodiscard]] bool run_max31865_connected_diagnostic() noexcept;

} // namespace smoker::platform

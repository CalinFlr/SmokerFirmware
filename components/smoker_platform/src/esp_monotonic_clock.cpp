#include "smoker/platform/esp_monotonic_clock.hpp"

#include "esp_timer.h"

#include <cstdint>

namespace smoker::platform {

core::MonotonicTimePoint EspMonotonicClock::now() const noexcept
{
    constexpr std::int64_t microseconds_per_millisecond = 1'000;
    return core::MonotonicTimePoint{
        core::Duration{esp_timer_get_time() / microseconds_per_millisecond}
    };
}

} // namespace smoker::platform

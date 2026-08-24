#pragma once

#include "smoker/app/ports.hpp"

namespace smoker::platform {

// ESP-IDF's microsecond timer is monotonic since boot. Application/domain time
// remains millisecond based, so sub-millisecond precision is intentionally
// truncated without introducing wall-clock or SNTP behavior.
class EspMonotonicClock final : public app::IClock {
public:
    [[nodiscard]] core::MonotonicTimePoint now() const noexcept override;
};

} // namespace smoker::platform

#pragma once

#include <cstdint>
#include <optional>

namespace smoker::platform {

// Returns UTC only after the platform clock has reached a credible SNTP-synced
// epoch. Monotonic control/session time remains authoritative.
[[nodiscard]] std::optional<std::int64_t> synchronized_unix_utc_now() noexcept;

} // namespace smoker::platform

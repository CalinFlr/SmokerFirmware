#include "smoker/platform/wall_clock.hpp"

#include <ctime>

namespace smoker::platform {

std::optional<std::int64_t> synchronized_unix_utc_now() noexcept
{
    constexpr std::time_t earliest_credible_epoch = 1'577'836'800; // 2020-01-01 UTC
    const auto now = std::time(nullptr);
    return now >= earliest_credible_epoch
        ? std::optional<std::int64_t>{static_cast<std::int64_t>(now)}
        : std::nullopt;
}

} // namespace smoker::platform

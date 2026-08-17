#pragma once

#include <optional>

namespace smoker::core {

class HeaterDemand final {
public:
    [[nodiscard]] static constexpr HeaterDemand off() noexcept
    {
        return HeaterDemand{0.0F};
    }

    [[nodiscard]] static std::optional<HeaterDemand> from_percent(float percent) noexcept;

    [[nodiscard]] constexpr float percent() const noexcept
    {
        return percent_;
    }

    [[nodiscard]] constexpr bool operator==(const HeaterDemand&) const noexcept = default;

private:
    explicit constexpr HeaterDemand(float percent) noexcept
        : percent_{percent}
    {
    }

    float percent_;
};

} // namespace smoker::core

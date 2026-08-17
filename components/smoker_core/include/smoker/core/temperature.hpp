#pragma once

#include <compare>
#include <optional>

namespace smoker::core {

class Temperature final {
public:
    [[nodiscard]] static std::optional<Temperature> from_celsius(float celsius) noexcept;

    [[nodiscard]] constexpr float celsius() const noexcept
    {
        return celsius_;
    }

    [[nodiscard]] constexpr auto operator<=>(const Temperature&) const noexcept = default;

private:
    explicit constexpr Temperature(float celsius) noexcept
        : celsius_{celsius}
    {
    }

    float celsius_;
};

} // namespace smoker::core

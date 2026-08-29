#pragma once

#include <cstdint>

namespace smoker::platform {

class Ads1115TargetBackend;

// This object's lifecycle state prevents restart and overlapping descriptor
// ownership only through this instance. Locked i2cdev 2.1.2 cannot safely
// restart after subsystem release, so activation must compose exactly one owner and
// permit exactly one initialization attempt per boot. Enforcing that
// composition-wide precondition here would require project-global mutable
// state, which this inactive boundary deliberately does not add.
class I2cdevSubsystemOwner final {
public:
    I2cdevSubsystemOwner() noexcept = default;
    ~I2cdevSubsystemOwner();

    I2cdevSubsystemOwner(const I2cdevSubsystemOwner&) = delete;
    I2cdevSubsystemOwner& operator=(const I2cdevSubsystemOwner&) = delete;
    I2cdevSubsystemOwner(I2cdevSubsystemOwner&&) = delete;
    I2cdevSubsystemOwner& operator=(I2cdevSubsystemOwner&&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool shutdown() noexcept;
    [[nodiscard]] bool active() const noexcept;

private:
    friend class Ads1115TargetBackend;

    enum class Lifecycle : std::uint8_t {
        NeverInitialized,
        Active,
        Released,
    };

    [[nodiscard]] bool acquire_descriptor_owner() noexcept;
    void release_descriptor_owner() noexcept;

    Lifecycle lifecycle_{Lifecycle::NeverInitialized};
    bool descriptor_owner_active_{false};
};

} // namespace smoker::platform

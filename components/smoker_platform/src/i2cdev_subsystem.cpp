#include "smoker/platform/i2cdev_subsystem.hpp"

#include <esp_err.h>
#include <i2cdev.h>

namespace smoker::platform {

I2cdevSubsystemOwner::~I2cdevSubsystemOwner()
{
    if (lifecycle_ != Lifecycle::Active || descriptor_owner_active_) return;
    static_cast<void>(i2cdev_done());
    lifecycle_ = Lifecycle::Released;
}

bool I2cdevSubsystemOwner::initialize() noexcept
{
    if (lifecycle_ != Lifecycle::NeverInitialized) return false;
    if (i2cdev_init() != ESP_OK) {
        // Exact 2.1.2 may have created some port mutexes before reporting an
        // initialization failure. No descriptor owner exists at this point.
        static_cast<void>(i2cdev_done());
        lifecycle_ = Lifecycle::Released;
        return false;
    }
    lifecycle_ = Lifecycle::Active;
    return true;
}

bool I2cdevSubsystemOwner::shutdown() noexcept
{
    if (lifecycle_ != Lifecycle::Active || descriptor_owner_active_) return false;
    const auto result = i2cdev_done();
    // Locked 2.1.2 does not reset i2cdev_init()'s function-local initialized
    // flag. This instance can reject its own restart, but cannot prevent a
    // different owner from receiving a misleading ESP_OK later in the boot.
    // Also, ESP_OK reports only i2cdev_done()'s aggregate API result; it cannot
    // rediscover cleanup errors previously swallowed by descriptor release.
    lifecycle_ = Lifecycle::Released;
    return result == ESP_OK;
}

bool I2cdevSubsystemOwner::active() const noexcept
{
    return lifecycle_ == Lifecycle::Active;
}

bool I2cdevSubsystemOwner::acquire_descriptor_owner() noexcept
{
    if (!active() || descriptor_owner_active_) return false;
    descriptor_owner_active_ = true;
    return true;
}

void I2cdevSubsystemOwner::release_descriptor_owner() noexcept
{
    descriptor_owner_active_ = false;
}

} // namespace smoker::platform

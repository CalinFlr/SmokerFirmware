#pragma once

#include "smoker/core/domain.hpp"

#include <optional>

namespace smoker::app {

class IChamberSensor {
public:
    virtual ~IChamberSensor() = default;
    // Critical-cycle implementations must complete in bounded time and must not
    // depend on network, storage, or another non-critical service.
    [[nodiscard]] virtual std::optional<core::Temperature> read() noexcept = 0;
};

class IFoodProbeSource {
public:
    virtual ~IFoodProbeSource() = default;
    [[nodiscard]] virtual std::optional<core::Temperature> read(core::ProbeId probe_id) noexcept = 0;
};

class IHeaterOutput {
public:
    virtual ~IHeaterOutput() = default;
    virtual void write(core::HeaterDemand demand) noexcept = 0;
};

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual core::MonotonicTimePoint now() const noexcept = 0;
};

class IEventSink {
public:
    virtual ~IEventSink() = default;
    // Must be bounded and non-blocking for the critical cycle. Publish locally;
    // network/storage forwarding belongs outside the heater dependency chain.
    virtual void publish(const core::Event& event) noexcept = 0;
};

} // namespace smoker::app

#pragma once

#include "smoker/app/ports.hpp"
#include "smoker/core/domain.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace smoker::platform {

class SimulatedClock final : public app::IClock {
public:
    [[nodiscard]] core::MonotonicTimePoint now() const noexcept override;
    void set(core::MonotonicTimePoint now) noexcept;
    void advance(core::Duration duration) noexcept;

private:
    core::MonotonicTimePoint now_{};
};

class SimulatedChamberSensor final : public app::IChamberSensor {
public:
    explicit SimulatedChamberSensor(std::optional<core::Temperature> reading) noexcept;

    [[nodiscard]] std::optional<core::Temperature> read() noexcept override;
    void set_reading(std::optional<core::Temperature> reading) noexcept;

private:
    std::optional<core::Temperature> reading_;
};

class SimulatedFoodProbeSource final : public app::IFoodProbeSource {
public:
    explicit SimulatedFoodProbeSource(std::span<const core::FoodProbeConfig> probes);

    [[nodiscard]] std::optional<core::Temperature> read(core::ProbeId probe_id) noexcept override;
    [[nodiscard]] bool set_reading(
        core::ProbeId probe_id,
        std::optional<core::Temperature> reading
    ) noexcept;

private:
    struct Entry final {
        core::ProbeId id{};
        std::optional<core::Temperature> reading;
    };

    std::vector<Entry> entries_;
};

class SimulatedHeaterOutput final : public app::IHeaterOutput {
public:
    SimulatedHeaterOutput();

    void write(core::HeaterDemand demand) noexcept override;
    [[nodiscard]] core::HeaterDemand last_demand() const noexcept;
    [[nodiscard]] std::span<const float> history() const noexcept;

private:
    static constexpr std::size_t history_capacity = 128U;

    core::HeaterDemand last_demand_{core::HeaterDemand::off()};
    std::array<float, history_capacity> history_{};
    std::size_t history_size_{0U};
};

class SimulatedEventSink final : public app::IEventSink {
public:
    static constexpr std::size_t event_capacity = 64U;

    void publish(const core::Event& event) noexcept override;
    [[nodiscard]] std::span<const core::Event> events() const noexcept;
    [[nodiscard]] std::size_t overwritten_event_count() const noexcept;
    void clear() noexcept;

private:
    std::array<core::Event, event_capacity> events_{};
    std::size_t event_count_{0U};
    std::size_t overwritten_event_count_{0U};
};

} // namespace smoker::platform

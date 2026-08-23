#include "smoker/platform/simulated_adapters.hpp"

#include "smoker/core/control.hpp"

#include <algorithm>

namespace smoker::platform {

core::MonotonicTimePoint SimulatedClock::now() const noexcept
{
    return now_;
}

void SimulatedClock::set(const core::MonotonicTimePoint now) noexcept
{
    if (now >= now_) {
        now_ = now;
    }
}

void SimulatedClock::advance(const core::Duration duration) noexcept
{
    if (duration >= core::Duration::zero()) {
        now_ += duration;
    }
}

SimulatedChamberSensor::SimulatedChamberSensor(
    std::optional<core::Temperature> reading
) noexcept
    : reading_{reading}
{
}

std::optional<core::Temperature> SimulatedChamberSensor::read() noexcept
{
    return reading_;
}

void SimulatedChamberSensor::set_reading(
    const std::optional<core::Temperature> reading
) noexcept
{
    reading_ = reading;
}

SimulatedFoodProbeSource::SimulatedFoodProbeSource(
    const std::span<const core::FoodProbeConfig> probes
)
{
    entries_.reserve(probes.size());
    for (const auto& probe : probes) {
        entries_.push_back(Entry{probe.id, std::nullopt});
    }
}

std::optional<core::Temperature> SimulatedFoodProbeSource::read(
    const core::ProbeId probe_id
) noexcept
{
    const auto entry = std::find_if(
        entries_.begin(), entries_.end(),
        [probe_id](const Entry& value) { return value.id == probe_id; }
    );
    return entry == entries_.end() ? std::nullopt : entry->reading;
}

bool SimulatedFoodProbeSource::set_reading(
    const core::ProbeId probe_id,
    const std::optional<core::Temperature> reading
) noexcept
{
    const auto entry = std::find_if(
        entries_.begin(), entries_.end(),
        [probe_id](const Entry& value) { return value.id == probe_id; }
    );
    if (entry == entries_.end()) {
        return false;
    }

    entry->reading = reading;
    return true;
}

std::optional<core::HeaterDemand> DeterministicChamberController::request(
    const core::Temperature chamber_temperature,
    const core::Temperature chamber_target
) noexcept
{
    return core::calculate_heater_demand(chamber_temperature, chamber_target);
}

bool DeterministicChamberController::reset() noexcept
{
    return true;
}

SimulatedHeaterOutput::SimulatedHeaterOutput() = default;

void SimulatedHeaterOutput::write(const core::HeaterDemand demand) noexcept
{
    last_demand_ = demand;
    if (history_size_ < history_.size()) {
        history_[history_size_] = demand.percent();
        ++history_size_;
    } else {
        history_.back() = demand.percent();
    }
}

core::HeaterDemand SimulatedHeaterOutput::last_demand() const noexcept
{
    return last_demand_;
}

std::span<const float> SimulatedHeaterOutput::history() const noexcept
{
    return std::span<const float>{history_.data(), history_size_};
}

void SimulatedEventSink::publish(const core::Event& event) noexcept
{
    if (event_count_ < events_.size()) {
        events_[event_count_] = event;
        ++event_count_;
        return;
    }

    // Keep a bounded, chronological view of the most recent events. The fixed
    // shift cost is deterministic and publish() never allocates or grows.
    std::move(events_.begin() + 1, events_.end(), events_.begin());
    events_.back() = event;
    ++overwritten_event_count_;
}

std::span<const core::Event> SimulatedEventSink::events() const noexcept
{
    return std::span<const core::Event>{events_.data(), event_count_};
}

std::size_t SimulatedEventSink::overwritten_event_count() const noexcept
{
    return overwritten_event_count_;
}

void SimulatedEventSink::clear() noexcept
{
    event_count_ = 0U;
    overwritten_event_count_ = 0U;
}

} // namespace smoker::platform

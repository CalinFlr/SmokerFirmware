#include "smoker/app/snapshot_exchange.hpp"

#include <algorithm>
#include <utility>

namespace smoker::app {

SnapshotExchange::ReadLease::ReadLease(
    const SnapshotExchange* const exchange, const std::size_t index
) noexcept
    : exchange_{exchange}
    , index_{index}
{
}

SnapshotExchange::ReadLease::ReadLease(ReadLease&& other) noexcept
    : exchange_{std::exchange(other.exchange_, nullptr)}
    , index_{other.index_}
{
}

SnapshotExchange::ReadLease& SnapshotExchange::ReadLease::operator=(ReadLease&& other) noexcept
{
    if (this != &other) {
        release();
        exchange_ = std::exchange(other.exchange_, nullptr);
        index_ = other.index_;
    }
    return *this;
}

SnapshotExchange::ReadLease::~ReadLease()
{
    release();
}

SmokerSnapshotView SnapshotExchange::ReadLease::view() const noexcept
{
    return exchange_ == nullptr ? SmokerSnapshotView{} : exchange_->view(index_);
}

SnapshotExchange::ReadLease::operator bool() const noexcept
{
    return exchange_ != nullptr;
}

void SnapshotExchange::ReadLease::release() noexcept
{
    if (exchange_ != nullptr) {
        exchange_->release(index_);
        exchange_ = nullptr;
    }
}

SnapshotExchange::SnapshotExchange(
    const std::size_t probe_capacity, const std::size_t alarm_capacity
)
{
    for (auto& slot : slots_) {
        slot.probes.resize(probe_capacity);
        slot.alarms.resize(alarm_capacity);
    }
}

bool SnapshotExchange::publish(const SmokerSnapshotView& snapshot) noexcept
{
    if (snapshot.probes.size() > slots_.front().probes.size()
        || snapshot.active_alarms.size() > slots_.front().alarms.size()
        || snapshot.command_results.size() > command_result_capacity) {
        dropped_publish_count_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    const auto current = published_index_.load(std::memory_order_acquire);
    std::size_t destination = no_slot;
    for (std::size_t index = 0U; index < slot_count; ++index) {
        if (index != current && readers_[index].load(std::memory_order_acquire) == 0U) {
            destination = index;
            break;
        }
    }
    if (destination == no_slot) {
        dropped_publish_count_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    auto& slot = slots_[destination];
    slot.session_status = snapshot.session_status;
    slot.session_id = snapshot.session_id;
    slot.session_elapsed = snapshot.session_elapsed;
    slot.stop_reason = snapshot.stop_reason;
    slot.chamber_temperature = snapshot.chamber_temperature;
    slot.chamber_target = snapshot.chamber_target;
    slot.heater_demand = snapshot.heater_demand;
    slot.timer = snapshot.timer;
    std::copy(snapshot.probes.begin(), snapshot.probes.end(), slot.probes.begin());
    slot.probe_count = snapshot.probes.size();
    std::copy(snapshot.active_alarms.begin(), snapshot.active_alarms.end(), slot.alarms.begin());
    slot.alarm_count = snapshot.active_alarms.size();
    slot.active_fault = snapshot.active_fault;
    slot.firmware_update_active = snapshot.firmware_update_active;
    slot.maximum_chamber_temperature = snapshot.maximum_chamber_temperature;
    slot.command_queue_overflow_count = snapshot.command_queue_overflow_count;
    std::copy(
        snapshot.command_results.begin(),
        snapshot.command_results.end(),
        slot.command_results.begin()
    );
    slot.command_result_count = snapshot.command_results.size();

    published_index_.store(destination, std::memory_order_release);
    return true;
}

SnapshotExchange::ReadLease SnapshotExchange::acquire() const noexcept
{
    while (true) {
        const auto index = published_index_.load(std::memory_order_acquire);
        if (index == no_slot) {
            return {};
        }

        readers_[index].fetch_add(1U, std::memory_order_acq_rel);
        if (published_index_.load(std::memory_order_acquire) == index) {
            return ReadLease{this, index};
        }
        readers_[index].fetch_sub(1U, std::memory_order_release);
    }
}

std::size_t SnapshotExchange::dropped_publish_count() const noexcept
{
    return dropped_publish_count_.load(std::memory_order_relaxed);
}

SmokerSnapshotView SnapshotExchange::view(const std::size_t index) const noexcept
{
    const auto& slot = slots_[index];
    return SmokerSnapshotView{
        slot.session_status,
        slot.session_id,
        slot.session_elapsed,
        slot.stop_reason,
        slot.chamber_temperature,
        slot.chamber_target,
        slot.heater_demand,
        slot.timer,
        std::span<const ProbeSnapshotView>{slot.probes.data(), slot.probe_count},
        std::span<const core::Alarm>{slot.alarms.data(), slot.alarm_count},
        slot.active_fault,
        slot.firmware_update_active,
        slot.maximum_chamber_temperature,
        slot.command_queue_overflow_count,
        std::span<const CommandResultView>{
            slot.command_results.data(), slot.command_result_count
        },
    };
}

void SnapshotExchange::release(const std::size_t index) const noexcept
{
    readers_[index].fetch_sub(1U, std::memory_order_release);
}

} // namespace smoker::app

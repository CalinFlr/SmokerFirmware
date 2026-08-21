#include "smoker/app/command_mailbox.hpp"

#include <variant>
#include <utility>

namespace smoker::app {

MailboxAdmission SpscCommandMailbox::push(
    Command command,
    const std::uint32_t correlation_id,
    const std::uint32_t transport_generation
)
{
    const bool is_stop = std::holds_alternative<StopSessionCommand>(command);
    const auto write = write_sequence_.load(std::memory_order_relaxed);
    const auto read = read_sequence_.load(std::memory_order_acquire);

    const auto pending_count = static_cast<std::size_t>(write - read);
    const auto admission_limit = is_stop ? capacity : regular_admission_capacity;
    if (pending_count >= admission_limit) {
        overflow_count_.fetch_add(1U, std::memory_order_relaxed);
        return MailboxAdmission::Full;
    }

    const auto index = static_cast<std::size_t>(write % capacity);
    if (commands_[index]) {
        commands_[index]->command = std::move(command);
        commands_[index]->correlation_id = correlation_id;
        commands_[index]->transport_generation = transport_generation;
    } else {
        commands_[index].emplace(TransportCommand{
            std::move(command), correlation_id, transport_generation
        });
    }
    write_sequence_.store(write + 1U, std::memory_order_release);
    return MailboxAdmission::Accepted;
}

bool SpscCommandMailbox::try_pop(
    Command& command,
    std::uint32_t* const correlation_id,
    std::uint32_t* const transport_generation
) noexcept
{
    const auto read = read_sequence_.load(std::memory_order_relaxed);
    const auto write = write_sequence_.load(std::memory_order_acquire);
    if (read == write) {
        return false;
    }

    const auto index = static_cast<std::size_t>(read % capacity);
    command = std::move(commands_[index]->command);
    if (correlation_id != nullptr) {
        *correlation_id = commands_[index]->correlation_id;
    }
    if (transport_generation != nullptr) {
        *transport_generation = commands_[index]->transport_generation;
    }
    read_sequence_.store(read + 1U, std::memory_order_release);
    return true;
}

std::size_t SpscCommandMailbox::pending() const noexcept
{
    const auto write = write_sequence_.load(std::memory_order_acquire);
    const auto read = read_sequence_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
}

std::size_t SpscCommandMailbox::overflow_count() const noexcept
{
    return overflow_count_.load(std::memory_order_relaxed);
}

} // namespace smoker::app

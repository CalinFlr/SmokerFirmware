#pragma once

#include "smoker/platform/flash_operation_coordinator.hpp"
#include "smoker/platform/history_support.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace smoker::platform {

enum class HistoryQueryResult : std::uint8_t { Ok, Busy, NotFound, Failed };

class HistoryService final {
public:
    HistoryService(
        HistoryObservationMailbox& mailbox,
        FlashOperationCoordinator& flash_operations
    ) noexcept;
    ~HistoryService();

    HistoryService(const HistoryService&) = delete;
    HistoryService& operator=(const HistoryService&) = delete;

    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] HistoryHealth health() const noexcept;
    [[nodiscard]] HistoryQueryResult sessions(
        std::optional<std::uint64_t> before,
        std::size_t limit,
        std::vector<HistorySessionSummary>& destination
    ) const;
    [[nodiscard]] HistoryQueryResult samples(
        std::uint64_t history_id,
        std::optional<std::uint32_t> after,
        std::size_t limit,
        std::uint16_t stride,
        std::vector<HistoryObservation>& destination,
        std::optional<std::uint32_t>& continuation
    ) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smoker::platform

#pragma once

#include "smoker/app/commands.hpp"
#include "smoker/core/domain.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace smoker::platform {

inline constexpr std::size_t blynk_inbound_capacity = 16U;
inline constexpr std::size_t blynk_datastream_capacity = 32U;
inline constexpr std::size_t blynk_command_payload_capacity = 64U;

enum class BlynkCommandDecision : std::uint8_t {
    Accepted,
    Ignored,
    Malformed,
    Deprecated,
};

enum class BlynkFirmwareOperation : std::uint8_t {
    None,
    Check,
    Install,
};

enum class BlynkInboundAdmission : std::uint8_t {
    Accepted,
    Ignored,
    Malformed,
    Full,
};

struct BlynkInboundCommand final {
    std::array<char, blynk_datastream_capacity> datastream{};
    std::array<char, blynk_command_payload_capacity> payload{};
    std::uint8_t datastream_length{0U};
    std::uint8_t payload_length{0U};
    std::uint32_t connection_generation{0U};

    [[nodiscard]] std::string_view datastream_view() const noexcept;
    [[nodiscard]] std::string_view payload_view() const noexcept;
};

// One ESP-MQTT callback producer and one BlynkTask consumer. The callback only
// validates the exact allowlist and copies bounded bytes. Regular requests
// cannot consume the final slot reserved for the first live Stop request.
class BlynkInboundMailbox final {
public:
    [[nodiscard]] BlynkInboundAdmission push(
        std::string_view datastream,
        std::string_view payload,
        std::uint32_t connection_generation = 1U
    ) noexcept;
    [[nodiscard]] bool try_pop(BlynkInboundCommand& destination) noexcept;
    [[nodiscard]] std::optional<std::uint32_t> front_connection_generation() const noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;
    [[nodiscard]] std::uint32_t dropped_count() const noexcept;

private:
    std::array<BlynkInboundCommand, blynk_inbound_capacity> commands_{};
    alignas(64) std::atomic<std::uint32_t> read_sequence_{0U};
    alignas(64) std::atomic<std::uint32_t> write_sequence_{0U};
    std::atomic<std::uint32_t> dropped_count_{0U};
};

struct BlynkMappedCommand final {
    BlynkCommandDecision decision{BlynkCommandDecision::Ignored};
    std::optional<app::Command> command;
    BlynkFirmwareOperation firmware_operation{BlynkFirmwareOperation::None};
};

// Deterministic fixed-buffer command translation. Atomic Start payloads and
// temperatures are parsed without retained cross-message state. Decimal
// parsing accepts only an optional minus sign, decimal digits, and at most one
// decimal point; it does not depend on floating-point from_chars support on
// Xtensa.
class BlynkCommandMapper final {
public:
    explicit BlynkCommandMapper(core::Recipe startup_recipe) noexcept;

    [[nodiscard]] BlynkMappedCommand map(
        std::string_view datastream,
        std::string_view payload,
        core::SessionId start_session_id
    ) noexcept;

private:
    core::Recipe startup_recipe_;
};

// Empty means the decision does not require remote protocol-error feedback.
[[nodiscard]] std::string_view blynk_command_error_message(
    BlynkCommandDecision decision
) noexcept;

[[nodiscard]] bool is_blynk_control_datastream(
    std::string_view datastream
) noexcept;

} // namespace smoker::platform

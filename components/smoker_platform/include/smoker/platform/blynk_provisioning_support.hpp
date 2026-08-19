#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace smoker::platform {

inline constexpr std::size_t blynk_endpoint_capacity = 96U;
inline constexpr std::size_t blynk_template_id_capacity = 64U;
inline constexpr std::size_t blynk_token_capacity = 192U;
inline constexpr std::size_t blynk_set_payload_capacity =
    6U + (blynk_endpoint_capacity - 1U)
    + (blynk_template_id_capacity - 1U)
    + (blynk_token_capacity - 1U);
inline constexpr std::size_t blynk_persisted_blob_size =
    4U + 2U + 6U + blynk_endpoint_capacity
    + blynk_template_id_capacity + blynk_token_capacity + 4U;

struct BlynkProvisionedConfiguration final {
    std::array<char, blynk_endpoint_capacity> endpoint{};
    std::array<char, blynk_template_id_capacity> template_id{};
    std::array<char, blynk_token_capacity> token{};

    friend bool operator==(
        const BlynkProvisionedConfiguration&,
        const BlynkProvisionedConfiguration&
    ) = default;
};

[[nodiscard]] bool valid_blynk_configuration(
    const BlynkProvisionedConfiguration& configuration
) noexcept;

[[nodiscard]] std::array<std::uint8_t, blynk_persisted_blob_size>
encode_blynk_configuration(
    const BlynkProvisionedConfiguration& configuration
) noexcept;

[[nodiscard]] std::optional<BlynkProvisionedConfiguration>
decode_blynk_configuration(std::span<const std::uint8_t> blob) noexcept;

[[nodiscard]] std::uint32_t blynk_frame_crc32(
    std::span<const std::uint8_t> bytes
) noexcept;

enum class BlynkProvisioningOperation : std::uint8_t {
    Set,
    Status,
    Clear,
};

enum class BlynkProvisioningParseError : std::uint8_t {
    None,
    MalformedHeader,
    Oversized,
    Corrupt,
    InvalidConfiguration,
};

struct BlynkProvisioningRequest final {
    BlynkProvisioningOperation operation{BlynkProvisioningOperation::Status};
    BlynkProvisionedConfiguration configuration{};
};

// Byte-stream parser for:
// FUMURI-BLYNK/1 <SET|STATUS|CLEAR> <length> <crc32-hex>\n<payload>
// SET payload is three big-endian uint16 lengths followed by endpoint,
// template ID, and token bytes. STATUS/CLEAR use length=0 and CRC=00000000.
class BlynkProvisioningParser final {
public:
    [[nodiscard]] std::optional<BlynkProvisioningRequest> consume(
        std::uint8_t byte
    ) noexcept;
    [[nodiscard]] BlynkProvisioningParseError take_error() noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] bool parse_header() noexcept;
    [[nodiscard]] std::optional<BlynkProvisioningRequest> finish_payload() noexcept;
    void fail(BlynkProvisioningParseError error) noexcept;

    std::array<char, 96U> header_{};
    std::size_t header_length_{0U};
    std::array<std::uint8_t, blynk_set_payload_capacity> payload_{};
    std::size_t payload_length_{0U};
    std::size_t expected_payload_length_{0U};
    std::uint32_t expected_crc_{0U};
    BlynkProvisioningOperation operation_{BlynkProvisioningOperation::Status};
    BlynkProvisioningParseError error_{BlynkProvisioningParseError::None};
    bool reading_payload_{false};
};

} // namespace smoker::platform

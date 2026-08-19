#include "smoker/platform/blynk_provisioning_support.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>

namespace smoker::platform {
namespace {

constexpr std::array<std::uint8_t, 4U> blob_magic{'F', 'B', 'L', 'K'};
constexpr std::uint16_t blob_version = 1U;

void put_u16(std::span<std::uint8_t> bytes, const std::uint16_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

[[nodiscard]] std::uint16_t get_u16(const std::span<const std::uint8_t> bytes) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U)
        | static_cast<std::uint16_t>(bytes[1])
    );
}

void put_u32(std::span<std::uint8_t> bytes, const std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    bytes[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

[[nodiscard]] std::uint32_t get_u32(const std::span<const std::uint8_t> bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24U)
        | (static_cast<std::uint32_t>(bytes[1]) << 16U)
        | (static_cast<std::uint32_t>(bytes[2]) << 8U)
        | static_cast<std::uint32_t>(bytes[3]);
}

template <std::size_t Size>
[[nodiscard]] std::size_t text_length(const std::array<char, Size>& value) noexcept
{
    const auto terminator = std::find(value.begin(), value.end(), '\0');
    return static_cast<std::size_t>(terminator - value.begin());
}

[[nodiscard]] bool regional_endpoint(const std::string_view endpoint) noexcept
{
    constexpr std::string_view suffix = ".blynk.cloud";
    if (endpoint.size() <= suffix.size() || !endpoint.ends_with(suffix)) return false;
    const auto region = endpoint.substr(0U, endpoint.size() - suffix.size());
    if (region.empty() || region.front() == '-' || region.back() == '-') return false;
    return std::all_of(region.begin(), region.end(), [](const char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9') || value == '-';
    });
}

[[nodiscard]] bool safe_identifier(const std::string_view value) noexcept
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const char byte) {
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-';
    });
}

[[nodiscard]] bool safe_token(const std::string_view value) noexcept
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](const char byte) {
        const auto character = static_cast<unsigned char>(byte);
        return character >= 0x21U && character <= 0x7EU;
    });
}

[[nodiscard]] bool split_header_token(
    const std::string_view header,
    std::size_t& cursor,
    std::string_view& token
) noexcept
{
    if (cursor >= header.size()) return false;
    const auto separator = header.find(' ', cursor);
    if (separator == std::string_view::npos) {
        token = header.substr(cursor);
        cursor = header.size();
    } else {
        token = header.substr(cursor, separator - cursor);
        cursor = separator + 1U;
    }
    return !token.empty();
}

} // namespace

bool valid_blynk_configuration(
    const BlynkProvisionedConfiguration& configuration
) noexcept
{
    const auto endpoint_length = text_length(configuration.endpoint);
    const auto template_length = text_length(configuration.template_id);
    const auto token_length = text_length(configuration.token);
    if (endpoint_length == configuration.endpoint.size()
        || template_length == configuration.template_id.size()
        || token_length == configuration.token.size()) {
        return false;
    }
    return regional_endpoint({configuration.endpoint.data(), endpoint_length})
        && safe_identifier({configuration.template_id.data(), template_length})
        && safe_token({configuration.token.data(), token_length});
}

std::uint32_t blynk_frame_crc32(
    const std::span<const std::uint8_t> bytes
) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

std::array<std::uint8_t, blynk_persisted_blob_size> encode_blynk_configuration(
    const BlynkProvisionedConfiguration& configuration
) noexcept
{
    std::array<std::uint8_t, blynk_persisted_blob_size> result{};
    std::size_t cursor = 0U;
    std::copy(blob_magic.begin(), blob_magic.end(), result.begin());
    cursor += blob_magic.size();
    put_u16({result.data() + cursor, 2U}, blob_version);
    cursor += 2U;
    put_u16({result.data() + cursor, 2U}, static_cast<std::uint16_t>(text_length(configuration.endpoint)));
    cursor += 2U;
    put_u16({result.data() + cursor, 2U}, static_cast<std::uint16_t>(text_length(configuration.template_id)));
    cursor += 2U;
    put_u16({result.data() + cursor, 2U}, static_cast<std::uint16_t>(text_length(configuration.token)));
    cursor += 2U;
    std::memcpy(result.data() + cursor, configuration.endpoint.data(), configuration.endpoint.size());
    cursor += configuration.endpoint.size();
    std::memcpy(result.data() + cursor, configuration.template_id.data(), configuration.template_id.size());
    cursor += configuration.template_id.size();
    std::memcpy(result.data() + cursor, configuration.token.data(), configuration.token.size());
    cursor += configuration.token.size();
    put_u32({result.data() + cursor, 4U}, blynk_frame_crc32({result.data(), cursor}));
    return result;
}

std::optional<BlynkProvisionedConfiguration> decode_blynk_configuration(
    const std::span<const std::uint8_t> blob
) noexcept
{
    if (blob.size() != blynk_persisted_blob_size
        || !std::equal(blob_magic.begin(), blob_magic.end(), blob.begin())) {
        return std::nullopt;
    }
    std::size_t cursor = blob_magic.size();
    if (get_u16(blob.subspan(cursor, 2U)) != blob_version) return std::nullopt;
    cursor += 2U;
    const auto endpoint_length = get_u16(blob.subspan(cursor, 2U)); cursor += 2U;
    const auto template_length = get_u16(blob.subspan(cursor, 2U)); cursor += 2U;
    const auto token_length = get_u16(blob.subspan(cursor, 2U)); cursor += 2U;
    if (endpoint_length >= blynk_endpoint_capacity
        || template_length >= blynk_template_id_capacity
        || token_length >= blynk_token_capacity) return std::nullopt;
    const auto crc_offset = blob.size() - 4U;
    if (blynk_frame_crc32(blob.first(crc_offset))
        != get_u32(blob.subspan(crc_offset, 4U))) return std::nullopt;

    BlynkProvisionedConfiguration result{};
    std::memcpy(result.endpoint.data(), blob.data() + cursor, blynk_endpoint_capacity);
    cursor += blynk_endpoint_capacity;
    std::memcpy(result.template_id.data(), blob.data() + cursor, blynk_template_id_capacity);
    cursor += blynk_template_id_capacity;
    std::memcpy(result.token.data(), blob.data() + cursor, blynk_token_capacity);
    result.endpoint[endpoint_length] = '\0';
    result.template_id[template_length] = '\0';
    result.token[token_length] = '\0';
    if (text_length(result.endpoint) != endpoint_length
        || text_length(result.template_id) != template_length
        || text_length(result.token) != token_length
        || !valid_blynk_configuration(result)) return std::nullopt;
    return result;
}

void BlynkProvisioningParser::fail(const BlynkProvisioningParseError error) noexcept
{
    error_ = error;
    header_length_ = 0U;
    payload_length_ = 0U;
    expected_payload_length_ = 0U;
    reading_payload_ = false;
}

void BlynkProvisioningParser::reset() noexcept
{
    header_length_ = 0U;
    payload_length_ = 0U;
    expected_payload_length_ = 0U;
    expected_crc_ = 0U;
    reading_payload_ = false;
}

BlynkProvisioningParseError BlynkProvisioningParser::take_error() noexcept
{
    const auto result = error_;
    error_ = BlynkProvisioningParseError::None;
    return result;
}

bool BlynkProvisioningParser::parse_header() noexcept
{
    const std::string_view header{header_.data(), header_length_};
    std::size_t cursor = 0U;
    std::string_view protocol;
    std::string_view operation;
    std::string_view length_text;
    std::string_view crc_text;
    if (!split_header_token(header, cursor, protocol)
        || !split_header_token(header, cursor, operation)
        || !split_header_token(header, cursor, length_text)
        || !split_header_token(header, cursor, crc_text)
        || cursor != header.size() || protocol != "FUMURI-BLYNK/1") {
        return false;
    }
    if (operation == "SET") operation_ = BlynkProvisioningOperation::Set;
    else if (operation == "STATUS") operation_ = BlynkProvisioningOperation::Status;
    else if (operation == "CLEAR") operation_ = BlynkProvisioningOperation::Clear;
    else return false;

    std::size_t length = 0U;
    const auto length_result = std::from_chars(
        length_text.data(), length_text.data() + length_text.size(), length
    );
    if (length_result.ec != std::errc{}
        || length_result.ptr != length_text.data() + length_text.size()) return false;
    if (length > blynk_set_payload_capacity) {
        fail(BlynkProvisioningParseError::Oversized);
        return false;
    }
    std::uint32_t crc = 0U;
    const auto crc_result = std::from_chars(
        crc_text.data(), crc_text.data() + crc_text.size(), crc, 16
    );
    if (crc_text.size() != 8U || crc_result.ec != std::errc{}
        || crc_result.ptr != crc_text.data() + crc_text.size()) return false;
    if ((operation_ == BlynkProvisioningOperation::Set && length < 9U)
        || (operation_ != BlynkProvisioningOperation::Set && (length != 0U || crc != 0U))) {
        return false;
    }
    expected_payload_length_ = length;
    expected_crc_ = crc;
    payload_length_ = 0U;
    return true;
}

std::optional<BlynkProvisioningRequest> BlynkProvisioningParser::finish_payload() noexcept
{
    if (blynk_frame_crc32({payload_.data(), payload_length_}) != expected_crc_) {
        fail(BlynkProvisioningParseError::Corrupt);
        return std::nullopt;
    }
    const auto endpoint_length = get_u16({payload_.data(), 2U});
    const auto template_length = get_u16({payload_.data() + 2U, 2U});
    const auto token_length = get_u16({payload_.data() + 4U, 2U});
    if (endpoint_length >= blynk_endpoint_capacity
        || template_length >= blynk_template_id_capacity
        || token_length >= blynk_token_capacity
        || 6U + endpoint_length + template_length + token_length != payload_length_) {
        fail(BlynkProvisioningParseError::InvalidConfiguration);
        return std::nullopt;
    }
    BlynkProvisionedConfiguration configuration{};
    std::size_t cursor = 6U;
    std::memcpy(configuration.endpoint.data(), payload_.data() + cursor, endpoint_length);
    cursor += endpoint_length;
    std::memcpy(configuration.template_id.data(), payload_.data() + cursor, template_length);
    cursor += template_length;
    std::memcpy(configuration.token.data(), payload_.data() + cursor, token_length);
    if (!valid_blynk_configuration(configuration)) {
        fail(BlynkProvisioningParseError::InvalidConfiguration);
        return std::nullopt;
    }
    reset();
    return BlynkProvisioningRequest{BlynkProvisioningOperation::Set, configuration};
}

std::optional<BlynkProvisioningRequest> BlynkProvisioningParser::consume(
    const std::uint8_t byte
) noexcept
{
    if (!reading_payload_) {
        if (byte == static_cast<std::uint8_t>('\r')) return std::nullopt;
        if (byte != static_cast<std::uint8_t>('\n')) {
            if (header_length_ == header_.size()) {
                fail(BlynkProvisioningParseError::Oversized);
                return std::nullopt;
            }
            header_[header_length_++] = static_cast<char>(byte);
            return std::nullopt;
        }
        if (!parse_header()) {
            if (error_ == BlynkProvisioningParseError::None) {
                fail(BlynkProvisioningParseError::MalformedHeader);
            }
            return std::nullopt;
        }
        header_length_ = 0U;
        if (expected_payload_length_ == 0U) {
            const auto operation = operation_;
            reset();
            return BlynkProvisioningRequest{operation, {}};
        }
        reading_payload_ = true;
        return std::nullopt;
    }

    payload_[payload_length_++] = byte;
    if (payload_length_ == expected_payload_length_) return finish_payload();
    return std::nullopt;
}

} // namespace smoker::platform

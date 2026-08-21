#include "smoker/platform/blynk_command_support.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <utility>
#include <variant>

namespace smoker::platform {
namespace {

constexpr std::array<std::string_view, 10U> control_datastreams{
    "CmdStart",
    "CmdStartTargetC",
    "CmdStop",
    "CmdChamberTargetC",
    "CmdProbeTarget",
    "CmdProbeEnabled",
    "CmdProbeAlarmEnabled",
    "CmdAcknowledgeAlarm",
    "CmdClearResolvedFault",
    "CmdFirmware",
};

[[nodiscard]] bool is_one(const std::string_view payload) noexcept
{
    return payload == "1";
}

[[nodiscard]] std::optional<std::uint32_t> parse_unsigned(
    const std::string_view text
) noexcept
{
    if (text.empty() || text.size() > 10U) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Returns a decimal value scaled by 1,000. Exponents, plus signs, whitespace,
// locale separators, NaN, and infinity are deliberately outside the grammar.
[[nodiscard]] std::optional<std::int32_t> parse_decimal_milli(
    const std::string_view text
) noexcept
{
    if (text.empty() || text.size() > 16U) {
        return std::nullopt;
    }
    std::size_t cursor = 0U;
    bool negative = false;
    if (text[cursor] == '-') {
        negative = true;
        ++cursor;
        if (cursor == text.size()) {
            return std::nullopt;
        }
    }

    std::uint32_t whole = 0U;
    std::uint32_t fraction = 0U;
    std::uint32_t fraction_scale = 100U;
    std::size_t whole_digits = 0U;
    std::size_t fraction_digits = 0U;
    bool decimal_point = false;
    for (; cursor < text.size(); ++cursor) {
        const char value = text[cursor];
        if (value == '.') {
            if (decimal_point) {
                return std::nullopt;
            }
            decimal_point = true;
            continue;
        }
        if (value < '0' || value > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint32_t>(value - '0');
        if (!decimal_point) {
            if (++whole_digits > 6U || whole > 100'000U) {
                return std::nullopt;
            }
            whole = (whole * 10U) + digit;
        } else {
            if (++fraction_digits > 3U) {
                return std::nullopt;
            }
            fraction += digit * fraction_scale;
            fraction_scale /= 10U;
        }
    }
    if (whole_digits == 0U || (decimal_point && fraction_digits == 0U)) {
        return std::nullopt;
    }
    const std::uint32_t magnitude = whole * 1000U + fraction;
    if (magnitude > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    const auto signed_value = static_cast<std::int32_t>(magnitude);
    return negative ? -signed_value : signed_value;
}

[[nodiscard]] std::optional<core::Temperature> parse_temperature(
    const std::string_view text,
    bool& valid
) noexcept
{
    valid = false;
    if (text == "-273.1") {
        valid = true;
        return std::nullopt;
    }
    const auto milli = parse_decimal_milli(text);
    if (!milli) {
        return std::nullopt;
    }
    const auto value = static_cast<float>(*milli) / 1000.0F;
    const auto temperature = core::Temperature::from_celsius(value);
    valid = temperature.has_value();
    return temperature;
}

[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> split_pair(
    const std::string_view payload
) noexcept
{
    const auto comma = payload.find(',');
    if (comma == std::string_view::npos || comma == 0U || comma + 1U >= payload.size()
        || payload.find(',', comma + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    return std::pair{payload.substr(0U, comma), payload.substr(comma + 1U)};
}

[[nodiscard]] BlynkMappedCommand malformed() noexcept
{
    return {
        BlynkCommandDecision::Malformed,
        std::nullopt,
        BlynkFirmwareOperation::None,
    };
}

} // namespace

bool is_blynk_control_datastream(const std::string_view datastream) noexcept
{
    return std::find(
        control_datastreams.begin(), control_datastreams.end(), datastream
    ) != control_datastreams.end();
}

std::string_view BlynkInboundCommand::datastream_view() const noexcept
{
    return {datastream.data(), datastream_length};
}

std::string_view BlynkInboundCommand::payload_view() const noexcept
{
    return {payload.data(), payload_length};
}

BlynkInboundAdmission BlynkInboundMailbox::push(
    const std::string_view datastream,
    const std::string_view payload,
    const std::uint32_t connection_generation
) noexcept
{
    if (!is_blynk_control_datastream(datastream)) {
        return BlynkInboundAdmission::Ignored;
    }
    if (connection_generation == 0U || datastream.empty()
        || datastream.size() >= blynk_datastream_capacity
        || payload.size() >= blynk_command_payload_capacity) {
        dropped_count_.fetch_add(1U, std::memory_order_relaxed);
        return BlynkInboundAdmission::Malformed;
    }

    const bool is_stop = datastream == "CmdStop" && payload == "1";
    const auto write = write_sequence_.load(std::memory_order_relaxed);
    const auto read = read_sequence_.load(std::memory_order_acquire);
    const auto pending_count = static_cast<std::size_t>(write - read);
    const auto limit = is_stop
        ? blynk_inbound_capacity : blynk_inbound_capacity - 1U;
    if (pending_count >= limit) {
        dropped_count_.fetch_add(1U, std::memory_order_relaxed);
        return BlynkInboundAdmission::Full;
    }

    auto& destination = commands_[write % blynk_inbound_capacity];
    destination.datastream.fill('\0');
    destination.payload.fill('\0');
    std::memcpy(destination.datastream.data(), datastream.data(), datastream.size());
    std::memcpy(destination.payload.data(), payload.data(), payload.size());
    destination.datastream_length = static_cast<std::uint8_t>(datastream.size());
    destination.payload_length = static_cast<std::uint8_t>(payload.size());
    destination.connection_generation = connection_generation;
    write_sequence_.store(write + 1U, std::memory_order_release);
    return BlynkInboundAdmission::Accepted;
}

std::optional<std::uint32_t>
BlynkInboundMailbox::front_connection_generation() const noexcept
{
    const auto read = read_sequence_.load(std::memory_order_relaxed);
    const auto write = write_sequence_.load(std::memory_order_acquire);
    if (read == write) return std::nullopt;
    return commands_[read % blynk_inbound_capacity].connection_generation;
}

bool BlynkInboundMailbox::try_pop(BlynkInboundCommand& destination) noexcept
{
    const auto read = read_sequence_.load(std::memory_order_relaxed);
    const auto write = write_sequence_.load(std::memory_order_acquire);
    if (read == write) {
        return false;
    }
    destination = commands_[read % blynk_inbound_capacity];
    read_sequence_.store(read + 1U, std::memory_order_release);
    return true;
}

std::size_t BlynkInboundMailbox::pending() const noexcept
{
    const auto write = write_sequence_.load(std::memory_order_acquire);
    const auto read = read_sequence_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
}

std::uint32_t BlynkInboundMailbox::dropped_count() const noexcept
{
    return dropped_count_.load(std::memory_order_relaxed);
}

BlynkCommandMapper::BlynkCommandMapper(core::Recipe startup_recipe) noexcept
    : startup_recipe_{std::move(startup_recipe)}
{
}

BlynkMappedCommand BlynkCommandMapper::map(
    const std::string_view datastream,
    const std::string_view payload,
    const core::SessionId start_session_id
) noexcept
{
    if (datastream == "CmdStartTargetC") {
        bool valid = false;
        const auto target = parse_temperature(payload, valid);
        if (!valid) {
            return malformed();
        }
        pending_start_target_ = target;
        pending_start_target_set_ = true;
        return {BlynkCommandDecision::Accepted, std::nullopt, BlynkFirmwareOperation::None};
    }
    if (datastream == "CmdStart") {
        if (!is_one(payload)) {
            return {BlynkCommandDecision::Ignored, std::nullopt, BlynkFirmwareOperation::None};
        }
        auto recipe = startup_recipe_;
        if (pending_start_target_set_) {
            recipe.stage.chamber_target = pending_start_target_;
            pending_start_target_set_ = false;
            pending_start_target_.reset();
        }
        return {
            BlynkCommandDecision::Accepted,
            app::Command{app::StartSessionCommand{start_session_id, std::move(recipe)}},
            BlynkFirmwareOperation::None,
        };
    }
    if (datastream == "CmdStop") {
        return is_one(payload)
            ? BlynkMappedCommand{BlynkCommandDecision::Accepted, app::Command{app::StopSessionCommand{}}, BlynkFirmwareOperation::None}
            : BlynkMappedCommand{BlynkCommandDecision::Ignored, std::nullopt, BlynkFirmwareOperation::None};
    }
    if (datastream == "CmdChamberTargetC") {
        bool valid = false;
        const auto target = parse_temperature(payload, valid);
        return valid
            ? BlynkMappedCommand{BlynkCommandDecision::Accepted, app::Command{app::SetChamberTargetCommand{target}}, BlynkFirmwareOperation::None}
            : malformed();
    }
    if (datastream == "CmdProbeTarget") {
        const auto pair = split_pair(payload);
        if (!pair) return malformed();
        const auto id = parse_unsigned(pair->first);
        bool valid = false;
        const auto target = parse_temperature(pair->second, valid);
        if (!id || *id > std::numeric_limits<core::ProbeId>::max() || !valid) {
            return malformed();
        }
        return {BlynkCommandDecision::Accepted, app::Command{app::SetProbeTargetCommand{static_cast<core::ProbeId>(*id), target}}, BlynkFirmwareOperation::None};
    }
    if (datastream == "CmdProbeEnabled" || datastream == "CmdProbeAlarmEnabled") {
        const auto pair = split_pair(payload);
        if (!pair) return malformed();
        const auto id = parse_unsigned(pair->first);
        if (!id || *id > std::numeric_limits<core::ProbeId>::max()
            || (pair->second != "0" && pair->second != "1")) {
            return malformed();
        }
        const bool enabled = pair->second == "1";
        if (datastream == "CmdProbeEnabled") {
            return {BlynkCommandDecision::Accepted, app::Command{app::SetProbeEnabledCommand{static_cast<core::ProbeId>(*id), enabled}}, BlynkFirmwareOperation::None};
        }
        return {BlynkCommandDecision::Accepted, app::Command{app::SetProbeAlarmEnabledCommand{static_cast<core::ProbeId>(*id), enabled}}, BlynkFirmwareOperation::None};
    }
    if (datastream == "CmdAcknowledgeAlarm") {
        const auto id = parse_unsigned(payload);
        return id
            ? BlynkMappedCommand{BlynkCommandDecision::Accepted, app::Command{app::AcknowledgeAlarmCommand{*id}}, BlynkFirmwareOperation::None}
            : malformed();
    }
    if (datastream == "CmdClearResolvedFault") {
        return is_one(payload)
            ? BlynkMappedCommand{BlynkCommandDecision::Accepted, app::Command{app::ClearResolvedFaultCommand{}}, BlynkFirmwareOperation::None}
            : BlynkMappedCommand{BlynkCommandDecision::Ignored, std::nullopt, BlynkFirmwareOperation::None};
    }
    if (datastream == "CmdFirmware") {
        if (payload == "1") return {BlynkCommandDecision::Accepted, std::nullopt, BlynkFirmwareOperation::Check};
        if (payload == "2") return {BlynkCommandDecision::Accepted, std::nullopt, BlynkFirmwareOperation::Install};
        return malformed();
    }
    return {BlynkCommandDecision::Ignored, std::nullopt, BlynkFirmwareOperation::None};
}

void BlynkCommandMapper::disconnected() noexcept
{
    pending_start_target_.reset();
    pending_start_target_set_ = false;
}

} // namespace smoker::platform

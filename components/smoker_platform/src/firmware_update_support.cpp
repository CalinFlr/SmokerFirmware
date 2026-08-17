#include "smoker/platform/firmware_update_support.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>
#include <utility>

namespace smoker::platform {
namespace {

constexpr std::uint8_t image_header_magic = 0xE9U;
constexpr std::uint32_t application_descriptor_magic = 0xABCD5432U;
constexpr std::size_t image_chip_id_offset = 12U;
constexpr std::size_t app_descriptor_offset =
    firmware_image_header_size + firmware_segment_header_size;
constexpr std::size_t app_version_offset = app_descriptor_offset + 16U;
constexpr std::size_t app_project_name_offset = app_version_offset + 32U;

template <std::size_t Size>
void copy_bounded(std::array<char, Size>& destination, const std::string_view source) noexcept
{
    destination.fill('\0');
    const auto length = std::min(source.size(), destination.size() - 1U);
    std::memcpy(destination.data(), source.data(), length);
}

std::optional<std::uint32_t> parse_component(const std::string_view text) noexcept
{
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::uint16_t read_u16_le(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset
) noexcept
{
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::uint32_t read_u32_le(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset
) noexcept
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

template <std::size_t Size>
bool copy_c_string_from_bytes(
    std::array<char, Size>& destination,
    const std::span<const std::uint8_t> source
) noexcept
{
    const auto terminator = std::find(source.begin(), source.end(), 0U);
    if (terminator == source.end()) {
        return false;
    }
    destination.fill('\0');
    const auto length = static_cast<std::size_t>(terminator - source.begin());
    std::memcpy(destination.data(), source.data(), length);
    return true;
}

} // namespace

FirmwareImageDescriptor FirmwareImageMetadata::descriptor() const noexcept
{
    return FirmwareImageDescriptor{project_name.data(), chip_id, version.data()};
}

std::optional<FirmwareImageMetadata> parse_firmware_image_metadata(
    const std::span<const std::uint8_t> prefix
) noexcept
{
    if (prefix.size() < firmware_metadata_prefix_size
        || prefix[0U] != image_header_magic
        || read_u32_le(prefix, app_descriptor_offset) != application_descriptor_magic) {
        return std::nullopt;
    }

    FirmwareImageMetadata metadata{};
    metadata.chip_id = read_u16_le(prefix, image_chip_id_offset);
    if (!copy_c_string_from_bytes(
            metadata.version,
            prefix.subspan(app_version_offset, metadata.version.size())
        )
        || !copy_c_string_from_bytes(
            metadata.project_name,
            prefix.subspan(app_project_name_offset, metadata.project_name.size())
        )) {
        return std::nullopt;
    }
    return metadata;
}

MonotonicDeadline::MonotonicDeadline(
    const std::int64_t started_at_microseconds,
    const std::int64_t duration_microseconds
) noexcept
{
    const auto bounded_start = std::max<std::int64_t>(started_at_microseconds, 0);
    const auto bounded_duration = std::max<std::int64_t>(duration_microseconds, 0);
    if (bounded_start
        > std::numeric_limits<std::int64_t>::max() - bounded_duration) {
        expires_at_microseconds_ = std::numeric_limits<std::int64_t>::max();
    } else {
        expires_at_microseconds_ = bounded_start + bounded_duration;
    }
}

bool MonotonicDeadline::expired(const std::int64_t now_microseconds) const noexcept
{
    return now_microseconds >= expires_at_microseconds_;
}

int MonotonicDeadline::remaining_milliseconds(
    const std::int64_t now_microseconds,
    const int maximum_wait_milliseconds
) const noexcept
{
    if (expired(now_microseconds) || maximum_wait_milliseconds <= 0) {
        return 0;
    }
    const auto bounded_now = std::max<std::int64_t>(now_microseconds, 0);
    const auto remaining_microseconds = expires_at_microseconds_ - bounded_now;
    const auto rounded_milliseconds = remaining_microseconds / 1000LL
        + (remaining_microseconds % 1000LL != 0 ? 1LL : 0LL);
    return static_cast<int>(std::min<std::int64_t>(
        rounded_milliseconds,
        static_cast<std::int64_t>(maximum_wait_milliseconds)
    ));
}

std::optional<SemanticVersion> SemanticVersion::parse(const std::string_view text) noexcept
{
    const auto first_dot = text.find('.');
    if (first_dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto second_dot = text.find('.', first_dot + 1U);
    if (second_dot == std::string_view::npos
        || text.find('.', second_dot + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = parse_component(text.substr(0U, first_dot));
    const auto minor = parse_component(text.substr(first_dot + 1U, second_dot - first_dot - 1U));
    const auto patch = parse_component(text.substr(second_dot + 1U));
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    return SemanticVersion{*major, *minor, *patch};
}

FirmwareDescriptorDecision validate_firmware_descriptor(
    const FirmwareImageDescriptor& descriptor,
    const SemanticVersion current_version
) noexcept
{
    if (descriptor.project_name != firmware_project_name) {
        return FirmwareDescriptorDecision::WrongProject;
    }
    if (descriptor.chip_id != firmware_target_chip_id) {
        return FirmwareDescriptorDecision::WrongTarget;
    }
    const auto candidate = SemanticVersion::parse(descriptor.version);
    if (!candidate) {
        return FirmwareDescriptorDecision::InvalidVersion;
    }
    return *candidate > current_version
        ? FirmwareDescriptorDecision::Newer
        : FirmwareDescriptorDecision::NotNewer;
}

const char* firmware_update_state_name(const FirmwareUpdateState state) noexcept
{
    switch (state) {
    case FirmwareUpdateState::Idle: return "IDLE";
    case FirmwareUpdateState::Checking: return "CHECKING";
    case FirmwareUpdateState::UpToDate: return "UP_TO_DATE";
    case FirmwareUpdateState::Available: return "AVAILABLE";
    case FirmwareUpdateState::WaitingPermission: return "WAITING_PERMISSION";
    case FirmwareUpdateState::Installing: return "INSTALLING";
    case FirmwareUpdateState::Rebooting: return "REBOOTING";
    case FirmwareUpdateState::Validating: return "VALIDATING";
    case FirmwareUpdateState::Failed: return "FAILED";
    }
    return "FAILED";
}

FirmwareUpdateCoordinator::FirmwareUpdateCoordinator(
    const std::string_view current_version
) noexcept
    : current_semantic_version_{SemanticVersion::parse(current_version)}
{
    copy_bounded(status_.current_version, current_version);
    if (!current_semantic_version_) {
        status_.state = FirmwareUpdateState::Failed;
        set_error("current_version_invalid");
    }
}

const FirmwareUpdateStatus& FirmwareUpdateCoordinator::status() const noexcept
{
    return status_;
}

bool FirmwareUpdateCoordinator::begin_check() noexcept
{
    if (!current_semantic_version_
        || status_.state == FirmwareUpdateState::Checking
        || status_.state == FirmwareUpdateState::WaitingPermission
        || status_.state == FirmwareUpdateState::Installing
        || status_.state == FirmwareUpdateState::Rebooting
        || status_.state == FirmwareUpdateState::Validating) {
        return false;
    }
    status_.state = FirmwareUpdateState::Checking;
    status_.progress_percent = 0U;
    status_.available_version.fill('\0');
    status_.error.fill('\0');
    available_semantic_version_.reset();
    refresh_installation_allowed();
    return true;
}

void FirmwareUpdateCoordinator::complete_check(
    const FirmwareImageDescriptor& descriptor
) noexcept
{
    if (status_.state != FirmwareUpdateState::Checking || !current_semantic_version_) {
        return;
    }
    switch (validate_firmware_descriptor(descriptor, *current_semantic_version_)) {
    case FirmwareDescriptorDecision::Newer:
        available_semantic_version_ = SemanticVersion::parse(descriptor.version);
        copy_bounded(status_.available_version, descriptor.version);
        status_.state = FirmwareUpdateState::Available;
        break;
    case FirmwareDescriptorDecision::NotNewer:
        status_.state = FirmwareUpdateState::UpToDate;
        break;
    case FirmwareDescriptorDecision::InvalidVersion:
        fail("image_version_invalid");
        return;
    case FirmwareDescriptorDecision::WrongProject:
        fail("image_project_invalid");
        return;
    case FirmwareDescriptorDecision::WrongTarget:
        fail("image_target_invalid");
        return;
    }
    refresh_installation_allowed();
}

void FirmwareUpdateCoordinator::fail(const std::string_view error) noexcept
{
    status_.state = FirmwareUpdateState::Failed;
    status_.progress_percent = 0U;
    installation_ready_ = false;
    if (permission_correlation_id_ != 0U) {
        finish_signal_ = true;
    }
    permission_correlation_id_ = 0U;
    set_error(error);
    refresh_installation_allowed();
}

bool FirmwareUpdateCoordinator::begin_install(
    const std::string_view requested_version,
    const core::SessionStatus session_status,
    const std::uint32_t permission_correlation_id
) noexcept
{
    if (status_.state != FirmwareUpdateState::Available
        || session_status == core::SessionStatus::Running
        || permission_correlation_id == 0U
        || requested_version != status_.available_version.data()
        || !available_semantic_version_) {
        return false;
    }
    status_.state = FirmwareUpdateState::WaitingPermission;
    status_.progress_percent = 0U;
    status_.error.fill('\0');
    status_.installation_allowed = false;
    permission_correlation_id_ = permission_correlation_id;
    installation_ready_ = false;
    finish_signal_ = false;
    return true;
}

void FirmwareUpdateCoordinator::cancel_install(
    const std::uint32_t permission_correlation_id
) noexcept
{
    if (status_.state != FirmwareUpdateState::WaitingPermission
        || permission_correlation_id_ != permission_correlation_id) {
        return;
    }
    status_.state = FirmwareUpdateState::Available;
    permission_correlation_id_ = 0U;
    installation_ready_ = false;
    refresh_installation_allowed();
}

void FirmwareUpdateCoordinator::observe_application_snapshot(
    const app::SmokerSnapshotView& snapshot
) noexcept
{
    if (status_.state != FirmwareUpdateState::WaitingPermission) {
        return;
    }
    for (const auto& result : snapshot.command_results) {
        if (result.correlation_id != permission_correlation_id_) {
            continue;
        }
        if (!result.semantic_accepted || !snapshot.firmware_update_active) {
            fail("installation_not_permitted");
            return;
        }
        status_.state = FirmwareUpdateState::Installing;
        installation_ready_ = true;
        return;
    }
}

bool FirmwareUpdateCoordinator::installation_ready() const noexcept
{
    return installation_ready_;
}

void FirmwareUpdateCoordinator::note_install_progress(const std::uint8_t percent) noexcept
{
    if (status_.state == FirmwareUpdateState::Installing) {
        status_.progress_percent = std::min<std::uint8_t>(percent, 100U);
    }
}

void FirmwareUpdateCoordinator::note_rebooting() noexcept
{
    if (status_.state == FirmwareUpdateState::Installing) {
        installation_ready_ = false;
        status_.state = FirmwareUpdateState::Rebooting;
        status_.progress_percent = 100U;
    }
}

void FirmwareUpdateCoordinator::begin_validation() noexcept
{
    status_.state = FirmwareUpdateState::Validating;
    status_.progress_percent = 0U;
    status_.installation_allowed = false;
    status_.error.fill('\0');
}

void FirmwareUpdateCoordinator::note_validation_succeeded() noexcept
{
    status_.state = FirmwareUpdateState::Idle;
    status_.progress_percent = 100U;
    permission_correlation_id_ = 0U;
    finish_signal_ = true;
    refresh_installation_allowed();
}

bool FirmwareUpdateCoordinator::consume_finish_signal() noexcept
{
    return std::exchange(finish_signal_, false);
}

std::uint32_t FirmwareUpdateCoordinator::permission_correlation_id() const noexcept
{
    return permission_correlation_id_;
}

void FirmwareUpdateCoordinator::set_error(const std::string_view error) noexcept
{
    copy_bounded(status_.error, error);
}

void FirmwareUpdateCoordinator::refresh_installation_allowed() noexcept
{
    status_.installation_allowed = status_.state == FirmwareUpdateState::Available;
}

} // namespace smoker::platform

#pragma once

#include "smoker/app/snapshot_view.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace smoker::platform {

inline constexpr char firmware_project_name[] = "smoker_controller";
inline constexpr std::uint16_t firmware_target_chip_id = 0x0009U;
inline constexpr char firmware_release_url[] =
    "https://github.com/CalinFlr/SmokerFirmware/releases/latest/download/"
    "smoker_controller.bin";

struct SemanticVersion final {
    std::uint32_t major{0U};
    std::uint32_t minor{0U};
    std::uint32_t patch{0U};

    [[nodiscard]] static std::optional<SemanticVersion> parse(
        std::string_view text
    ) noexcept;

    friend constexpr bool operator==(const SemanticVersion&, const SemanticVersion&) = default;
    friend constexpr auto operator<=>(const SemanticVersion&, const SemanticVersion&) = default;
};

struct FirmwareImageDescriptor final {
    std::string_view project_name;
    std::uint16_t chip_id{0U};
    std::string_view version;
};

inline constexpr std::size_t firmware_image_header_size = 24U;
inline constexpr std::size_t firmware_segment_header_size = 8U;
inline constexpr std::size_t firmware_app_descriptor_size = 256U;
inline constexpr std::size_t firmware_metadata_prefix_size =
    firmware_image_header_size + firmware_segment_header_size
    + firmware_app_descriptor_size;

struct FirmwareImageMetadata final {
    std::uint16_t chip_id{0U};
    std::array<char, 32U> project_name{};
    std::array<char, 32U> version{};

    [[nodiscard]] FirmwareImageDescriptor descriptor() const noexcept;
};

[[nodiscard]] std::optional<FirmwareImageMetadata> parse_firmware_image_metadata(
    std::span<const std::uint8_t> prefix
) noexcept;

class MonotonicDeadline final {
public:
    MonotonicDeadline(std::int64_t started_at_microseconds, std::int64_t duration_microseconds) noexcept;

    [[nodiscard]] bool expired(std::int64_t now_microseconds) const noexcept;
    [[nodiscard]] int remaining_milliseconds(
        std::int64_t now_microseconds,
        int maximum_wait_milliseconds
    ) const noexcept;

private:
    std::int64_t expires_at_microseconds_{0};
};

enum class FirmwareDescriptorDecision : std::uint8_t {
    Newer,
    NotNewer,
    InvalidVersion,
    WrongProject,
    WrongTarget,
};

[[nodiscard]] FirmwareDescriptorDecision validate_firmware_descriptor(
    const FirmwareImageDescriptor& descriptor,
    SemanticVersion current_version
) noexcept;

enum class FirmwareUpdateState : std::uint8_t {
    Idle,
    Checking,
    UpToDate,
    Available,
    WaitingPermission,
    Installing,
    Rebooting,
    Validating,
    Failed,
};

[[nodiscard]] const char* firmware_update_state_name(FirmwareUpdateState state) noexcept;

struct FirmwareUpdateStatus final {
    FirmwareUpdateState state{FirmwareUpdateState::Idle};
    std::array<char, 32U> current_version{};
    std::array<char, 32U> available_version{};
    std::uint8_t progress_percent{0U};
    bool installation_allowed{false};
    std::array<char, 128U> error{};
};

// Platform-independent state policy. The ESP implementation serializes access
// to it; host tests exercise all semantic transitions without networking.
class FirmwareUpdateCoordinator final {
public:
    explicit FirmwareUpdateCoordinator(std::string_view current_version) noexcept;

    [[nodiscard]] const FirmwareUpdateStatus& status() const noexcept;
    [[nodiscard]] bool begin_check() noexcept;
    void complete_check(const FirmwareImageDescriptor& descriptor) noexcept;
    void fail(std::string_view error) noexcept;

    [[nodiscard]] bool begin_install(
        std::string_view requested_version,
        core::SessionStatus session_status,
        std::uint32_t permission_correlation_id
    ) noexcept;
    void cancel_install(std::uint32_t permission_correlation_id) noexcept;
    void observe_application_snapshot(const app::SmokerSnapshotView& snapshot) noexcept;
    [[nodiscard]] bool installation_ready() const noexcept;
    void note_install_progress(std::uint8_t percent) noexcept;
    void note_rebooting() noexcept;
    void begin_validation() noexcept;
    void note_validation_succeeded() noexcept;

    [[nodiscard]] bool consume_finish_signal() noexcept;
    [[nodiscard]] std::uint32_t permission_correlation_id() const noexcept;

private:
    void set_error(std::string_view error) noexcept;
    void refresh_installation_allowed() noexcept;

    FirmwareUpdateStatus status_{};
    std::optional<SemanticVersion> current_semantic_version_;
    std::optional<SemanticVersion> available_semantic_version_;
    std::uint32_t permission_correlation_id_{0U};
    bool installation_ready_{false};
    bool finish_signal_{false};
};

} // namespace smoker::platform

#include "smoker/app/smoker_application.hpp"
#include "smoker/platform/firmware_update_support.hpp"
#include "smoker/platform/simulated_adapters.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace {

class TestContext final {
public:
    void expect(const bool condition, const std::string_view description) noexcept
    {
        if (!condition) {
            ++failures;
            std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(description.size()), description.data());
        }
    }

    int failures{0};
};

smoker::core::Temperature temperature(const float value)
{
    return *smoker::core::Temperature::from_celsius(value);
}

smoker::core::Recipe recipe()
{
    return smoker::core::Recipe{
        1U,
        "M13 test",
        smoker::core::Stage{1U, "Single stage", temperature(110.0F), std::nullopt},
    };
}

void test_application_update_permission(TestContext& context)
{
    const std::array probes{
        smoker::core::FoodProbeConfig{
            1U,
            "Probe",
            smoker::core::ProbeRole::Meat,
            temperature(75.0F),
            true,
            true,
        },
    };
    smoker::platform::SimulatedChamberSensor chamber{temperature(25.0F)};
    smoker::platform::SimulatedFoodProbeSource probe_source{probes};
    smoker::platform::SimulatedHeaterOutput heater;
    smoker::platform::SimulatedClock clock;
    smoker::platform::SimulatedEventSink events;
    smoker::app::SmokerApplication application{
        chamber,
        probe_source,
        heater,
        clock,
        events,
        smoker::core::SafetyLimits{temperature(150.0F)},
        probes,
    };

    context.expect(
        application.submit(smoker::app::PrepareFirmwareUpdateCommand{}, 10U),
        "M13 application admits an IDLE firmware reservation"
    );
    application.tick();
    context.expect(
        application.snapshot().firmware_update_active,
        "M13 reservation is application-owned and snapshot-visible"
    );

    context.expect(
        application.submit(smoker::app::StartSessionCommand{1U, recipe()}, 11U),
        "M13 Start admission remains asynchronous during update"
    );
    application.tick();
    auto snapshot = application.snapshot();
    context.expect(
        snapshot.session_status == smoker::core::SessionStatus::Idle
            && snapshot.firmware_update_active,
        "M13 Start is semantically rejected while update permission is active"
    );
    context.expect(
        !snapshot.command_results.empty()
            && !snapshot.command_results.back().semantic_accepted,
        "M13 rejected Start has a correlated semantic result"
    );

    context.expect(
        application.submit(smoker::app::FinishFirmwareUpdateCommand{}, 12U),
        "M13 internal finish command is admitted"
    );
    application.tick();
    context.expect(
        !application.snapshot().firmware_update_active,
        "M13 failed/completed update releases the Start interlock"
    );

    context.expect(
        application.submit(smoker::app::StartSessionCommand{2U, recipe()}, 13U),
        "M13 Start is admitted after finish"
    );
    application.tick();
    context.expect(
        application.snapshot().session_status == smoker::core::SessionStatus::Running,
        "M13 Start succeeds after update interlock release"
    );
    context.expect(
        application.submit(smoker::app::PrepareFirmwareUpdateCommand{}, 14U),
        "M13 RUNNING reservation reaches semantic policy"
    );
    application.tick();
    snapshot = application.snapshot();
    context.expect(
        snapshot.session_status == smoker::core::SessionStatus::Running
            && !snapshot.firmware_update_active
            && !snapshot.command_results.back().semantic_accepted,
        "M13 installation is rejected during RUNNING without changing control"
    );

    context.expect(
        application.submit(smoker::app::StopSessionCommand{}, 15U)
            && application.submit(
                smoker::app::PrepareFirmwareUpdateCommand{}, 16U
            )
            && application.submit(
                smoker::app::FinishFirmwareUpdateCommand{}, 17U
            ),
        "M13 admits ordered Stop, Prepare, and Finish commands"
    );
    application.tick();
    snapshot = application.snapshot();
    context.expect(
        snapshot.session_status == smoker::core::SessionStatus::Stopped
            && !snapshot.firmware_update_active
            && snapshot.command_results.back().correlation_id == 15U,
        "M13 Stop remains an OFF-cycle barrier ahead of OTA permission"
    );
    application.tick();
    snapshot = application.snapshot();
    context.expect(
        !snapshot.firmware_update_active
            && snapshot.command_results.size() >= 2U
            && snapshot.command_results[snapshot.command_results.size() - 2U]
                   .correlation_id
                == 16U
            && snapshot.command_results[snapshot.command_results.size() - 2U]
                   .semantic_accepted
            && snapshot.command_results.back().correlation_id == 17U
            && snapshot.command_results.back().semantic_accepted,
        "M13 FIFO applies Prepare before Finish after a Stop barrier"
    );
}

void test_semantic_versions_and_descriptors(TestContext& context)
{
    using smoker::platform::FirmwareDescriptorDecision;
    using smoker::platform::FirmwareImageDescriptor;
    using smoker::platform::SemanticVersion;

    const auto current = SemanticVersion::parse("0.13.0");
    context.expect(current.has_value(), "M13 parses the project semantic version");
    context.expect(
        !SemanticVersion::parse("01.13.0") && !SemanticVersion::parse("0.13")
            && !SemanticVersion::parse("0.13.0-rc1"),
        "M13 rejects non-canonical or extended version strings"
    );
    context.expect(
        smoker::platform::validate_firmware_descriptor(
            FirmwareImageDescriptor{
                "smoker_controller", smoker::platform::firmware_target_chip_id, "0.13.1"
            },
            *current
        ) == FirmwareDescriptorDecision::Newer,
        "M13 accepts only a strictly newer matching image"
    );
    context.expect(
        smoker::platform::validate_firmware_descriptor(
            FirmwareImageDescriptor{
                "smoker_controller", smoker::platform::firmware_target_chip_id, "0.13.0"
            },
            *current
        ) == FirmwareDescriptorDecision::NotNewer
            && smoker::platform::validate_firmware_descriptor(
                FirmwareImageDescriptor{
                    "smoker_controller", smoker::platform::firmware_target_chip_id, "0.12.99"
                },
                *current
            ) == FirmwareDescriptorDecision::NotNewer,
        "M13 rejects equal and older images"
    );
    context.expect(
        smoker::platform::validate_firmware_descriptor(
            FirmwareImageDescriptor{
                "other", smoker::platform::firmware_target_chip_id, "0.13.1"
            },
            *current
        ) == FirmwareDescriptorDecision::WrongProject
            && smoker::platform::validate_firmware_descriptor(
                FirmwareImageDescriptor{"smoker_controller", 0U, "0.13.1"}, *current
            ) == FirmwareDescriptorDecision::WrongTarget,
        "M13 rejects wrong-project and wrong-target descriptors"
    );
}

void write_u32_le(
    std::span<std::uint8_t> bytes,
    const std::size_t offset,
    const std::uint32_t value
) noexcept
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void test_image_metadata_and_deadlines(TestContext& context)
{
    using smoker::platform::FirmwareDescriptorDecision;
    using smoker::platform::MonotonicDeadline;

    std::array<std::uint8_t, smoker::platform::firmware_metadata_prefix_size> prefix{};
    constexpr std::size_t descriptor_offset =
        smoker::platform::firmware_image_header_size
        + smoker::platform::firmware_segment_header_size;
    prefix[0U] = 0xE9U;
    prefix[12U] = static_cast<std::uint8_t>(
        smoker::platform::firmware_target_chip_id & 0xFFU
    );
    prefix[13U] = static_cast<std::uint8_t>(
        smoker::platform::firmware_target_chip_id >> 8U
    );
    write_u32_le(prefix, descriptor_offset, 0xABCD5432U);
    constexpr std::string_view version = "0.13.1";
    constexpr std::string_view project = "smoker_controller";
    std::memcpy(prefix.data() + descriptor_offset + 16U, version.data(), version.size());
    std::memcpy(prefix.data() + descriptor_offset + 48U, project.data(), project.size());

    const auto metadata = smoker::platform::parse_firmware_image_metadata(prefix);
    context.expect(
        metadata && metadata->chip_id == smoker::platform::firmware_target_chip_id
            && metadata->descriptor().project_name == project
            && metadata->descriptor().version == version,
        "M13 parses the real chip ID and descriptor fields from the image prefix"
    );

    prefix[12U] = 0U;
    prefix[13U] = 0U;
    const auto wrong_target = smoker::platform::parse_firmware_image_metadata(prefix);
    context.expect(
        wrong_target
            && smoker::platform::validate_firmware_descriptor(
                wrong_target->descriptor(), *smoker::platform::SemanticVersion::parse("0.13.0")
            ) == FirmwareDescriptorDecision::WrongTarget,
        "M13 production metadata parsing makes wrong-target rejection reachable"
    );
    prefix[0U] = 0U;
    context.expect(
        !smoker::platform::parse_firmware_image_metadata(prefix),
        "M13 rejects an invalid ESP image header before offering an update"
    );

    const MonotonicDeadline deadline{1'000'000LL, 10'000'000LL};
    context.expect(
        !deadline.expired(10'999'999LL)
            && deadline.remaining_milliseconds(1'000'000LL, 5'000) == 5'000
            && deadline.remaining_milliseconds(10'999'999LL, 5'000) == 1
            && deadline.expired(11'000'000LL)
            && deadline.remaining_milliseconds(11'000'000LL, 5'000) == 0,
        "M13 operation deadlines cap individual waits and expire monotonically"
    );
    const MonotonicDeadline saturated{
        std::numeric_limits<std::int64_t>::max() - 5LL, 10LL
    };
    context.expect(
        !saturated.expired(std::numeric_limits<std::int64_t>::max() - 1LL)
            && saturated.remaining_milliseconds(0LL, 5'000) == 5'000
            && saturated.expired(std::numeric_limits<std::int64_t>::max()),
        "M13 deadline construction and rounding saturate instead of overflowing"
    );
}

void test_update_coordinator(TestContext& context)
{
    smoker::platform::FirmwareUpdateCoordinator coordinator{"0.13.0"};
    context.expect(coordinator.begin_check(), "M13 begins a manual check");
    coordinator.complete_check({
        "smoker_controller", smoker::platform::firmware_target_chip_id, "0.13.1"
    });
    context.expect(
        coordinator.status().state == smoker::platform::FirmwareUpdateState::Available
            && coordinator.status().installation_allowed,
        "M13 exposes an available newer version"
    );
    context.expect(
        !coordinator.begin_install(
            "0.13.1", smoker::core::SessionStatus::Running, 50U
        ),
        "M13 coordinator rejects installation during RUNNING"
    );
    context.expect(
        coordinator.begin_install(
            "0.13.1", smoker::core::SessionStatus::Stopped, 51U
        ),
        "M13 coordinator waits for application permission while stopped"
    );
    coordinator.cancel_install(51U);
    context.expect(
        coordinator.status().state == smoker::platform::FirmwareUpdateState::Available
            && coordinator.status().installation_allowed
            && !coordinator.installation_ready(),
        "M13 coordinator restores availability if permission publication fails"
    );
    context.expect(
        coordinator.begin_install(
            "0.13.1", smoker::core::SessionStatus::Stopped, 52U
        ),
        "M13 coordinator can retry after a cancelled permission publication"
    );

    const std::array results{smoker::app::CommandResultView{52U, true}};
    smoker::app::SmokerSnapshotView snapshot{};
    snapshot.session_status = smoker::core::SessionStatus::Stopped;
    snapshot.firmware_update_active = true;
    snapshot.command_results = results;
    coordinator.observe_application_snapshot(snapshot);
    context.expect(
        coordinator.status().state == smoker::platform::FirmwareUpdateState::Installing
            && coordinator.installation_ready(),
        "M13 install starts only after correlated semantic permission"
    );
    coordinator.note_install_progress(63U);
    context.expect(
        coordinator.status().progress_percent == 63U,
        "M13 reports bounded installation progress"
    );
    coordinator.fail("download_failed");
    context.expect(
        coordinator.status().state == smoker::platform::FirmwareUpdateState::Failed
            && coordinator.consume_finish_signal()
            && !coordinator.consume_finish_signal(),
        "M13 failure emits one bounded application-finish signal"
    );
}

} // namespace

int main()
{
    TestContext context;
    test_application_update_permission(context);
    test_semantic_versions_and_descriptors(context);
    test_image_metadata_and_deadlines(context);
    test_update_coordinator(context);
    if (context.failures == 0) {
        std::puts("M13 OTA policy tests: PASS");
        return 0;
    }
    std::fprintf(stderr, "M13 OTA policy tests: %d failure(s)\n", context.failures);
    return 1;
}

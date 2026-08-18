#!/usr/bin/env python3
"""Fail when the M0-M14 source tree violates approved architecture boundaries."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
INTERNAL_COMPONENTS = {"smoker_core", "smoker_app", "smoker_platform"}


class CheckFailures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.messages.append(message)

    def finish(self) -> int:
        if self.messages:
            print("Architecture guardrails failed:", file=sys.stderr)
            for message in self.messages:
                print(f"  - {message}", file=sys.stderr)
            return 1
        print("Architecture guardrails: PASS")
        return 0


def source_files(*relative_roots: str) -> list[Path]:
    files: list[Path] = []
    for relative_root in relative_roots:
        base = ROOT / relative_root
        files.extend(
            path for path in base.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
    return sorted(files)


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def source_section(source: str, start_marker: str, end_marker: str) -> str:
    _, start, remainder = source.partition(start_marker)
    if not start:
        return ""
    section, end, _ = remainder.partition(end_marker)
    return start_marker + section if end else ""


def include_targets(path: Path) -> list[str]:
    return re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', path.read_text(), re.MULTILINE)


def check_layer_includes(failures: CheckFailures) -> None:
    forbidden_platform_prefixes = (
        "driver/",
        "esp_",
        "freertos/",
        "hal/",
        "lwip/",
        "nvs",
        "soc/",
    )
    layer_rules = {
        "components/smoker_core": ("smoker/app/", "smoker/platform/", *forbidden_platform_prefixes),
        "components/smoker_app": ("smoker/platform/", *forbidden_platform_prefixes),
    }
    for layer, forbidden in layer_rules.items():
        for path in source_files(layer):
            for target in include_targets(path):
                failures.require(
                    not target.startswith(forbidden),
                    f"{relative(path)} includes forbidden dependency {target!r}",
                )


def find_calls(pattern: str, paths: list[Path]) -> list[tuple[Path, int]]:
    expression = re.compile(pattern)
    matches: list[tuple[Path, int]] = []
    for path in paths:
        text = path.read_text()
        matches.extend((path, match.start()) for match in expression.finditer(text))
    return matches


def check_control_ownership(failures: CheckFailures) -> None:
    production = source_files("components", "main")
    expected_runtime = "components/smoker_platform/src/simulation_runtime.cpp"
    expected_connectivity = "components/smoker_platform/src/local_connectivity.cpp"
    expected_ota = "components/smoker_platform/src/firmware_update_service.cpp"
    expected_history = "components/smoker_platform/src/history_service.cpp"
    expected_history_support = "components/smoker_platform/src/history_support.cpp"
    expected_application = "components/smoker_app/src/smoker_application.cpp"

    task_calls = find_calls(r"\bxTaskCreate(?:Static)?(?:PinnedToCore)?\s*\(", production)
    failures.require(
        len(task_calls) == 4,
        "M14 must contain only ControlTask, OtaTask, HistoryTask, and the captive DNS helper task; "
        f"found {len(task_calls)} task-creation calls",
    )
    task_owners = {relative(path) for path, _ in task_calls}
    failures.require(
        task_owners == {expected_runtime, expected_connectivity, expected_ota, expected_history},
        "task creation must be limited to ControlTask, OtaTask, HistoryTask, and captive DNS; "
        f"found owners {sorted(task_owners)}",
    )

    output_writes = find_calls(r"\.\s*write\s*\(", production)
    heater_writes = [(path, offset) for path, offset in output_writes
                     if relative(path) == expected_application]
    failures.require(
        len(heater_writes) == 2,
        "the M5 composition must retain exactly the boot-OFF and final gated heater writes; "
        f"found {len(heater_writes)} application write calls",
    )
    for path, _ in output_writes:
        failures.require(
            relative(path) in {expected_application, expected_history_support},
            "member writes must be either heater-port writes or HistoryTask-owned raw-log "
            f"writes; found one in {relative(path)}",
        )

    submit_calls = find_calls(r"\.\s*submit\s*\(", production)
    failures.require(
        len(submit_calls) == 1,
        f"M12 production must have one ControlTask-owned submit call site; found {len(submit_calls)}",
    )
    for path, _ in submit_calls:
        failures.require(
            relative(path) == expected_runtime,
            f"production submit calls are allowed only in {expected_runtime}, found in {relative(path)}",
        )


def cmake_internal_dependencies(path: Path) -> set[str]:
    text = path.read_text()
    return {component for component in INTERNAL_COMPONENTS if re.search(rf"\b{component}\b", text)}


def check_component_graph(failures: CheckFailures) -> None:
    component_dirs = {
        path.name for path in (ROOT / "components").iterdir() if path.is_dir()
    }
    failures.require(
        component_dirs == INTERNAL_COMPONENTS,
        "components/ must contain exactly smoker_core, smoker_app, and smoker_platform; "
        f"found {sorted(component_dirs)}",
    )

    dependency_contract = {
        "smoker_core": (set(), set()),
        "smoker_app": ({"smoker_core"}, {"smoker_core"}),
        "smoker_platform": ({"smoker_app"}, {"smoker_app", "smoker_core"}),
    }
    for component, (required, allowed) in dependency_contract.items():
        path = ROOT / "components" / component / "CMakeLists.txt"
        dependencies = cmake_internal_dependencies(path) - {component}
        failures.require(
            required <= dependencies,
            f"{component} is missing required internal dependencies {sorted(required - dependencies)}",
        )
        failures.require(
            dependencies <= allowed,
            f"{component} has forbidden internal dependencies {sorted(dependencies - allowed)}",
        )

    main_dependencies = cmake_internal_dependencies(ROOT / "main/CMakeLists.txt")
    failures.require(
        main_dependencies == {"smoker_app", "smoker_platform"},
        "main must depend directly on exactly smoker_app and smoker_platform; "
        f"found {sorted(main_dependencies)}",
    )


def enum_members(text: str, enum_name: str) -> list[str] | None:
    match = re.search(rf"enum\s+class\s+{enum_name}\s*\{{(?P<body>.*?)\}}\s*;", text, re.DOTALL)
    if match is None:
        return None
    return [
        member.strip().split("=")[0].strip()
        for member in match.group("body").split(",")
        if member.strip()
    ]


def check_v0_scope(failures: CheckFailures) -> None:
    domain = (ROOT / "components/smoker_core/include/smoker/core/domain.hpp").read_text()
    failures.require(
        enum_members(domain, "SessionStatus") == ["Idle", "Running", "Stopped", "Fault"],
        "SessionStatus must contain exactly Idle, Running, Stopped, and Fault",
    )

    commands = (ROOT / "components/smoker_app/include/smoker/app/commands.hpp").read_text()
    command_variant = re.search(r"using\s+Command\s*=\s*std::variant\s*<(.*?)>\s*;", commands, re.DOTALL)
    expected_commands = {
        "StartSessionCommand",
        "StopSessionCommand",
        "SetChamberTargetCommand",
        "SetProbeTargetCommand",
        "SetProbeEnabledCommand",
        "SetProbeAlarmEnabledCommand",
        "AcknowledgeAlarmCommand",
        "ClearResolvedFaultCommand",
        "PrepareFirmwareUpdateCommand",
        "FinishFirmwareUpdateCommand",
    }
    actual_commands = set()
    if command_variant is not None:
        actual_commands = {
            value.strip() for value in command_variant.group(1).split(",") if value.strip()
        }
    failures.require(
        actual_commands == expected_commands,
        "the M5 Command variant changed outside the approved command family; "
        f"found {sorted(actual_commands)}",
    )

    production_text = "\n".join(path.read_text() for path in source_files("components", "main"))
    for forbidden_identifier in (
        "FoodSafetyRule",
        "FoodSafe",
        "PauseSessionCommand",
        "FanOutput",
        "SmokeGenerator",
    ):
        failures.require(
            re.search(rf"\b{forbidden_identifier}\b", production_text) is None,
            f"future/deferred identifier {forbidden_identifier} is present in M5 production code",
        )
    failures.require(
        re.search(r"struct\s+Recipe\s+final\s*\{.*?\bStage\s+stage\s*;", domain, re.DOTALL)
        is not None,
        "Recipe must retain exactly one scalar Stage in V0",
    )

    app_main = ROOT / "main/app_main.cpp"
    source_lines = [
        line for line in app_main.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    ]
    failures.require(
        len(source_lines) <= 80,
        f"main/app_main.cpp must remain a thin composition root (found {len(source_lines)} lines)",
    )


def check_reproducible_build_contract(failures: CheckFailures) -> None:
    root_cmake = (ROOT / "CMakeLists.txt").read_text()
    failures.require(
        'set(SMOKER_REQUIRED_ESP_IDF_VERSION "6.0.2")' in root_cmake,
        "root CMake must declare the exact ESP-IDF 6.0.2 requirement",
    )
    failures.require(
        "smoker_detected_esp_idf_version STREQUAL SMOKER_REQUIRED_ESP_IDF_VERSION"
        in root_cmake,
        "root CMake must fail configuration when the exact IDF version differs",
    )
    failures.require(
        '"-std=gnu++26"\n    "-std=c++20"' in root_cmake,
        "root CMake must replace ESP-IDF GNU++26 with strict C++20",
    )

    verification = (ROOT / "tools/verify.sh").read_text()
    failures.require(
        '[[ "$idf_version" != "ESP-IDF v6.0.2" ]]' in verification,
        "verification must compare the complete ESP-IDF version string exactly",
    )
    failures.require(
        "check_target_compile_commands.py" in verification,
        "target verification must inspect effective project C++ compile flags",
    )
    failures.require(
        "check_effective_sdkconfig.py" in verification,
        "target verification must inspect the effective generated M14 configuration",
    )
    flash_rejection = (ROOT / "tools/reject_unsigned_flash.cmake").read_text()
    failures.require(
        "smoker_signed_flash_required" in root_cmake
        and "tools/reject_unsigned_flash.cmake" in root_cmake
        and all(
            target in root_cmake
            for target in (
                "flash",
                "app-flash",
                "bootloader-flash",
                "partition-table-flash",
                "otadata-flash",
            )
        )
        and "FATAL_ERROR" in flash_rejection
        and "tools/flash_signed_firmware.py" in flash_rejection,
        "ordinary M14 ESP-IDF flash targets must fail closed and point to the signed helper",
    )


def check_m12_transport_contract(failures: CheckFailures) -> None:
    runtime = (ROOT / "components/smoker_platform/src/simulation_runtime.cpp").read_text()
    connectivity = (ROOT / "components/smoker_platform/src/local_connectivity.cpp").read_text()
    network_support = (
        ROOT / "components/smoker_platform/src/local_network_support.cpp"
    ).read_text()
    application = (ROOT / "components/smoker_app/src/smoker_application.cpp").read_text()
    web_assets = (ROOT / "components/smoker_platform/src/web_assets.hpp").read_text()
    decisions = (ROOT / "docs/DECISIONS.md").read_text()
    defaults = (ROOT / "sdkconfig.defaults").read_text()
    platform_cmake = (ROOT / "components/smoker_platform/CMakeLists.txt").read_text()
    manifest = (ROOT / "components/smoker_platform/idf_component.yml").read_text()
    lock = (ROOT / "dependencies.lock").read_text()
    ignore = (ROOT / ".gitignore").read_text()
    browser_fixture = (ROOT / "tools/m12_browser_fixture.py").read_text()
    browser_fixture_check = (ROOT / "tools/check_m12_http_fixture.py").read_text()
    browser_check = (ROOT / "tools/check_m12_browser.sh").read_text()
    m12_tests = (ROOT / "tests/host/smoker_m12_tests.cpp").read_text()

    failures.require(
        "context->mailbox.try_pop(command, &correlation_id)" in runtime
        and "submit_to_application(std::move(command), correlation_id)" in runtime,
        "ControlTask must be the mailbox consumer and sole application submit owner",
    )
    failures.require(
        "const auto snapshot = context->application.snapshot_view()" in runtime
        and "context->snapshots.publish(snapshot)" in runtime
        and "snapshot.chamber_target" in runtime,
        "ControlTask must publish and diagnose from one immutable post-tick snapshot",
    )
    failures.require(
        re.search(r"xTaskCreateStaticPinnedToCore\(.*?\n\s*1\s*\n\s*\);", runtime, re.DOTALL)
        is not None,
        "ControlTask must be pinned to core 1",
    )
    failures.require(
        ".submit(" not in connectivity,
        "HTTP/connectivity code must never call SmokerApplication::submit()",
    )
    failures.require(
        "configuration.core_id = 0;" in connectivity,
        "the HTTP server task must be pinned to core 0",
    )
    failures.require(
        '"SmokerDns"' in connectivity
        and "dns_stack_bytes = 4096U" in connectivity
        and "dns_task_exited_" in connectivity
        and "reap_exited_dns_task()" in connectivity
        and re.search(
            r'xTaskCreateStaticPinnedToCore\(.*?"SmokerDns".*?\n\s*0\s*\n\s*\);',
            connectivity,
            re.DOTALL,
        ) is not None,
        "the only auxiliary network task must be a static 4 KiB captive DNS task on core 0",
    )
    failures.require(
        connectivity.count('std::pair{"/api/v1/network/scan",') == 2
        and "ESP_NETIF_CAPTIVEPORTAL_URI" in connectivity
        and 'captive_portal_uri[] = "http://192.168.4.1/"' in connectivity
        and 'std::pair{"/api/v1/setup/status", HTTP_GET}' in connectivity
        and 'std::pair{"/api/v1/setup/network", HTTP_PUT}' in connectivity
        and "send_setup_redirect(request)" in connectivity
        and "request_scope(request)" in connectivity
        and "getsockname(" in connectivity
        and "classify_http_request_scope" in connectivity
        and "host_matches_device_authority" in connectivity
        and "return_to_ap_if_unconfigured()" in connectivity
        and "connect_sta_or_retry()" in connectivity
        and '"SmokerStaConnect"' in connectivity
        and "STA startup failed; exposing SoftAP recovery" in connectivity
        and "sta_configuration_transition_" in connectivity
        and "Ignoring stale GOT_IP" in connectivity
        and "esp_wifi_sta_get_ap_info" in connectivity
        and "apply_updated_wifi_configuration_locked()" in connectivity
        and "sta_connect_in_flight_" in connectivity
        and "configuration_disconnect_pending_" in connectivity
        and '"Location": "http://192.168.4.1/"' in browser_fixture
        and "httpd_register_err_handler" in connectivity,
        "M12 must retain scoped scan/setup routes and absolute captive setup discovery",
    )
    failures.require(
        'std::pair{"/login", HTTP_GET}' in connectivity
        and 'std::pair{"/login", HTTP_POST}' in connectivity
        and 'std::pair{"/login.js", HTTP_GET}' in connectivity
        and 'std::pair{"/api/v1/auth/session", HTTP_POST}' in connectivity
        and 'std::pair{"/api/v1/auth/session", HTTP_DELETE}' in connectivity
        and 'std::pair{"/api/v1/auth/password", HTTP_PUT}' in connectivity
        and "%s=%s; Path=/; HttpOnly; SameSite=Lax" in connectivity
        and "session_cookie_valid(request)" in connectivity
        and 'name="password"' in web_assets
        and 'name="username"' not in web_assets
        and 'id="toggle-password"' in web_assets
        and "WWW-Authenticate" not in connectivity
        and 'http_username[] = "admin"' not in connectivity
        and "AuthenticationMethod::Basic" not in connectivity
        and connectivity.index("request_scope(request)") < connectivity.index("authenticate(request)")
        and "check_m12_http_fixture.py" in (ROOT / "tools/verify.sh").read_text()
        and "Basic/admin must never grant access" in browser_fixture_check
        and "AP must reject" in browser_fixture_check,
        "M12 must retain scope-before-auth and password-to-cookie sessions without Basic",
    )
    failures.require(
        "active target draft lost" in browser_check
        and "probe target draft lost" in browser_check
        and "focused probe froze live reading" in browser_check
        and "snapshot polling overlapped" in browser_check
        and "unsupported STA network selectable" in browser_check
        and "Controlerul a respins" in browser_check
        and "document.querySelector('#active-target').value === ''" in browser_check
        and "document.querySelector('[data-probe-target=\\\"1\\\"]').value === '63'"
        in browser_check
        and "session cookie flags missing" in browser_check
        and "--port 0 --ready-file" in browser_check
        and "commissioning_port" in browser_check
        and "valid cookie unlocked snapshot through AP" in browser_check
        and "password change/logout controls missing" in browser_check
        and "login requested username" in browser_check
        and "password reveal failed" in browser_check
        and "document.activeElement !== activeTarget" in web_assets
        and "async function pollForever" in web_assets
        and "setInterval(" not in web_assets,
        "M12 must retain versioned real-browser coverage for auth and polling-safe edits",
    )
    failures.require(
        "stop_flags_" not in (ROOT / "components/smoker_app").joinpath(
            "include/smoker/app/command_mailbox.hpp"
        ).read_text()
        and "CoalescedStop" not in (
            ROOT / "components/smoker_app/src/command_mailbox.cpp"
        ).read_text(),
        "the cross-core mailbox must not use racy trailing-Stop coalescing",
    )
    failures.require(
        "std::atomic_bool wifi_running_" in connectivity,
        "cross-callback Wi-Fi running state must remain atomic",
    )
    failures.require(
        "WifiFallbackCoordinator fallback_coordinator_" in connectivity
        and "repeated STA disconnects do not postpone the fallback deadline" in m12_tests
        and "wifi_mode_mutex_" in connectivity,
        "M12 fallback deadline and AP/STA mode transitions must remain race-safe",
    )
    failures.require(
        "WifiStaRetryState sta_retry_state_" in connectivity
        and "WifiStaRetryAction::ReapplyConfiguration" in connectivity
        and "self->apply_updated_wifi_configuration()" in connectivity
        and "mode/configuration failure retries the complete STA configuration" in m12_tests,
        "failed STA driver configuration must be reapplied before reconnecting",
    )
    failures.require(
        "maximum_body_receive_timeouts" in connectivity
        and "body_receive_deadline_microseconds" in connectivity
        and "configuration.recv_wait_timeout = 2U" in connectivity
        and connectivity.count("receive_timeouts++ >= maximum_body_receive_timeouts") == 2,
        "M12 HTTP body reads must have a finite timeout budget",
    )
    failures.require(
        "peer.ss_family == AF_INET6" in connectivity
        and "extract_ipv4_mapped_address(" in connectivity[
            connectivity.rindex("request_peer_key(") : connectivity.index(
                "send_asset(", connectivity.rindex("request_peer_key(")
            )
        ]
        and "build_http_error_body(message)" in connectivity
        and 'std::string{"{\\"error\\":\\""}' not in connectivity,
        "dual-stack login peers and low-memory HTTP errors must retain bounded handling",
    )
    failures.require(
        'nvs_wifi_configuration_key[] = "sta_config_v1"' in connectivity
        and 'nvs_authentication_configuration_key[] = "auth_config_v1"' in connectivity
        and connectivity.count("return nvs_set_blob(") == 2
        and "nvs_set_str(" not in connectivity
        and "load_string(nvs_ssid_key" in connectivity
        and "load_string(\n                nvs_wifi_password_key" in connectivity
        and "load_string(\n                nvs_device_password_key" in connectivity
        and "## D048 — M12 credential groups use atomic versioned NVS blobs" in decisions,
        "Wi-Fi and device-auth companion fields must use atomic versioned blobs with legacy migration",
    )
    failures.require(
        "access_point.ap.authmode = WIFI_AUTH_OPEN;" in connectivity
        and "access_point.ap.password" not in connectivity
        and "generate_local_credential" not in connectivity
        and 'constexpr char initial_device_password[] = "smoker257500";' in connectivity
        and "configuration_.device_password, initial_device_password" in connectivity
        and "LoginRateLimiter login_rate_limiter_" in connectivity
        and "http_origin_allowed(" in connectivity
        and "## D046 — The commissioning SoftAP is intentionally open" in decisions
        and "## D047 — SoftAP is commissioning-only and LAN auth is password-to-session" in decisions
        and "HTTP Basic" in decisions
        and "STA OPEN" in decisions
        and "Adding WPA2, generating" in decisions
        and "Reviewers must retain and report this product decision" in decisions,
        "commissioning must retain the open AP and fixed password while forbidding AP control, Basic, and STA OPEN",
    )
    for color in ("#ee7d3b", "#bb5728", "#83a89e", "#c6b780", "#f2eee5", "#171918"):
        failures.require(color in web_assets.lower(), f"Fumuri UI token is missing: {color}")
    app_script = re.search(
        r'inline constexpr std::string_view app_js = R"JS\((.*?)\)JS";',
        web_assets,
        re.DOTALL,
    )
    failures.require(
        app_script is not None and len(app_script.group(1).encode()) <= 40 * 1024,
        "embedded app.js must remain readable but bounded to 40 KiB",
    )
    failures.require(
        "https://" not in web_assets
        and "http://" not in web_assets
        and "@import" not in web_assets
        and "max-scale" not in web_assets
        and "maximum-scale" not in web_assets,
        "embedded Fumuri assets must remain external-resource-free and must not disable zoom",
    )
    for setting in (
        "CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0=y",
        "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y",
        "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0=y",
    ):
        failures.require(setting in defaults, f"M12 core-isolation setting is missing: {setting}")

    failures.require(
        'espressif/cjson: "==1.7.19"' in manifest
        and 'espressif/mdns: "==1.8.2"' in manifest,
        "M12 registry dependency versions must be exact",
    )
    failures.require(
        "espressif/cjson:" in lock and "version: 1.7.19" in lock
        and "espressif/mdns:" in lock and "version: 1.8.2" in lock
        and "version: 6.0.2" in lock,
        "dependencies.lock must pin cJSON, mDNS, and ESP-IDF",
    )
    failures.require(
        'esp-idf-lib/max31865: "==1.0.8"' in manifest
        and "esp-idf-lib__max31865" in platform_cmake
        and "esp-idf-lib/max31865:" in lock
        and "component_hash: c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f"
        in lock
        and "version: 1.0.8" in lock,
        "M7 preparation must retain the exact registry MAX31865 component and hash",
    )
    failures.require(
        'esp-idf-lib/ads111x: "==1.1.14"' in manifest
        and "esp-idf-lib__ads111x" in platform_cmake
        and "esp-idf-lib/ads111x:" in lock
        and "component_hash: fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2"
        in lock
        and "esp-idf-lib/i2cdev:" in lock
        and "component_hash: ad8981cc64533dcaced5107d72e42bcebe79345e194e82795792af531b300ce3"
        in lock
        and "esp-idf-lib/esp_idf_lib_helpers:" in lock
        and "component_hash: 689853bb8993434f9556af0f2816e808bf77b5d22100144b21f3519993daf237"
        in lock,
        "M9 preparation must retain the exact registry ADS1115 component and locked I2C support",
    )
    failures.require(
        "/managed_components/" in ignore,
        "generated managed_components must remain ignored",
    )
    partitions = (ROOT / "partitions.csv").read_text()
    verification = (ROOT / "tools/verify.sh").read_text()
    release_signing = (ROOT / "tools/sign_release_firmware.sh").read_text()
    effective_config = (ROOT / "tools/check_effective_sdkconfig.py").read_text()
    serial_flash = (ROOT / "tools/flash_signed_firmware.py").read_text()
    failures.require(
        "CONFIG_PARTITION_TABLE_CUSTOM=y" in defaults
        and 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"' in defaults
        and "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y" in defaults
        and "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" in defaults
        and "ota_0" in partitions and "ota_1" in partitions,
        "M13 custom dual-OTA layout, certificate bundle, or rollback setting is missing",
    )
    failures.require(
        "--partition-size 3145728" in verification
        and "check_partitions.py" in verification,
        "M13 must verify the exact partition table and 75% of each 3 MiB slot",
    )
    for setting in (
        "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y",
        "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y",
        "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y",
        "# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set",
    ):
        failures.require(
            setting in defaults,
            f"M13 signed-OTA configuration is missing: {setting}",
        )
    failures.require(
        "/local-secrets/" in ignore
        and "*.private.pem" in ignore
        and "smoker_ota_signing_public.pem" in release_signing
        and "check_effective_sdkconfig.py" in release_signing
        and "secure-sign-data" in release_signing
        and "secure-verify-signature" in release_signing
        and 'rm -f "$signing_key"' in release_signing
        and 'chmod 0644 "$signed_image"' in release_signing,
        "M13 release signing must isolate the private key, verify against the public key, and publish a readable artifact",
    )
    for setting in (
        "CONFIG_PARTITION_TABLE_CUSTOM",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE",
        "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT",
        "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT",
        "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES",
    ):
        failures.require(
            setting in effective_config,
            f"effective M13 configuration guard is missing: {setting}",
        )
    failures.require(
        "configuration_failures" in serial_flash
        and "secure-verify-signature" in serial_flash
        and "compare_unsigned_prefix" in serial_flash
        and "generated_partition_offsets" in serial_flash
        and "expected_offsets" in serial_flash
        and '"bootloader", "partition-table", "otadata"' in serial_flash
        and 'command.append("write-flash")' in serial_flash
        and 'parser.error("serial flashing requires both --port PORT and --yes")'
        in serial_flash,
        "M13 serial migration must validate effective config, signature, matching build, "
        "complete generated flash map, and explicit operator intent",
    )

    mailbox_header = (ROOT / "components/smoker_app/include/smoker/app/command_mailbox.hpp").read_text()
    snapshot_view = (ROOT / "components/smoker_app/include/smoker/app/snapshot_view.hpp").read_text()
    failures.require(
        "std::atomic<std::uint32_t> read_sequence_" in mailbox_header
        and "std::atomic<std::uint64_t>" not in mailbox_header
        and "CommandResultView" in snapshot_view
        and "build_http_command_admission_body(correlation_id)" in connectivity
        and '\\"command_id\\"' in network_support,
        "M12 command transport must retain 32-bit sequences and correlated semantic results",
    )
    command_handler = source_section(
        connectivity,
        "esp_err_t handle_command(",
        "[[nodiscard]] bool default_password_warning",
    )
    command_response = command_handler.find(
        "const auto response = build_http_command_admission_body(correlation_id)"
    )
    command_publish = command_handler.find("command_mailbox_.push(")
    command_send = command_handler.find("response.bytes.data()", command_publish)
    failures.require(
        command_response >= 0
        and command_publish > command_response
        and command_send > command_publish
        and "CjsonPointer" not in command_handler[command_response:command_send],
        "command admission JSON must be fixed and complete before mailbox publication",
    )
    failures.require(
        "network_status.ap_active" in connectivity
        and "!commissioning_active && sta_ipv4" in network_support
        and "rejects operational scope during open-AP overlap" in m12_tests,
        "the open commissioning AP must fail closed against operational STA scope",
    )
    failures.require(
        "decide_legacy_authentication_migration(" in connectivity
        and "LegacyAuthenticationMigrationAction::Reject" in connectivity
        and "claimed-without-password legacy state" in m12_tests,
        "legacy authentication migration must reject unreadable or inconsistent state",
    )
    failures.require(
        "coalesced_correlation_ids" in application
        and "coalesced_correlation_count" in application
        and "inherits the original Stop semantic rejection" in (
            ROOT / "tests/host/smoker_v0_tests.cpp"
        ).read_text(),
        "coalesced Stop IDs must inherit the processed Stop semantic result",
    )
    failures.require(
        "scan_timeout_microseconds" in connectivity
        and "scan_timeout_callback" in connectivity
        and '"supported"' in connectivity
        and '"last_error"' in connectivity,
        "M12 Wi-Fi scan timeout, supported-security, and actionable status contracts are missing",
    )

    decisions = (ROOT / "docs/DECISIONS.md").read_text()
    decision_ids = [int(value) for value in re.findall(r"^## D(\d{3})\b", decisions, re.MULTILINE)]
    failures.require(
        decision_ids == list(range(1, 58)),
        f"decision IDs must remain ordered and contiguous through D057; found {decision_ids}",
    )
    failures.require(
        "## D051 — Missing independent release review is conditional on single-maintainer access"
        in decisions
        and "maintainer-reported" in decisions
        and "additional human or automation" in decisions,
        "D051 must preserve the conditional single-maintainer P3 release boundary",
    )
    failures.require(
        "## D052 — Public canonical repository begins from one sanitized root commit"
        in decisions
        and "uncredentialed" in decisions
        and "GitHub access credential" in decisions
        and "exactly one root commit" in decisions
        and "must never turn every commit into" in decisions,
        "D052 must preserve public credential-free distribution and sanitized-root history",
    )
    failures.require(
        "## D053 — M14 uses a bounded raw-flash circular session history" in decisions
        and "commit-last" in decisions and "0x620000" in decisions
        and "HistoryTask" in decisions and "0x5e0000" in decisions,
        "D053 must preserve M14 storage, isolation, and layout decisions",
    )
    failures.require(
        "## D054 — M15 uses Blynk as a personal MQTT relay and application" in decisions
        and "Device MQTT API over TLS" in decisions
        and "no periodic duplicate status" in decisions
        and "five seconds" in decisions
        and "sync or replay Start or OTA-install datastream values" in decisions
        and "releases/latest/download/smoker_controller.bin" in decisions,
        "D054 must preserve the personal Blynk, change-driven status, no-replay, and fixed-OTA boundaries",
    )
    failures.require(
        "## D055 — M8 uses Espressif's official PID component behind a platform adapter"
        in decisions
        and "espressif/pid_ctrl" in decisions
        and "0.3.1" in decisions
        and "not added before M8" in decisions
        and "smoker_platform" in decisions
        and "synchronous safety gate is applied after PID computation" in decisions,
        "D055 must preserve the exact-pinned official PID component, pure-core, and safety-gate boundaries",
    )
    failures.require(
        "## D056 — M7 imports the registry MAX31865 driver before physical activation"
        in decisions
        and "esp-idf-lib/max31865" in decisions
        and "exactly at version 1.0.8" in decisions
        and "c7a027843a3f9cf4b06e7e216b25b2089115568f288c8682defd84c018a5b80f"
        in decisions
        and "max31865_measure()" in decisions
        and "Production continues to compose `SimulatedChamberSensor`" in decisions
        and "synchronous safety latches `ChamberSensorInvalid`" in decisions,
        "D056 must preserve the exact-pinned MAX31865 dependency, physical gate, and fail-OFF boundary",
    )
    failures.require(
        "## D057 — M9 imports the registry ADS1115 driver before physical activation"
        in decisions
        and "esp-idf-lib/ads111x" in decisions
        and "exactly at version 1.1.14" in decisions
        and "fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2"
        in decisions
        and "two distinct physical ADDR selections" in decisions
        and "compose simulated probe sources" in decisions
        and "no ADS1115 value or failure directly changes" in decisions,
        "D057 must preserve the exact-pinned dual-ADS1115 dependency, physical gate, and monitoring-only boundary",
    )

    workflow = "\n".join(
        path.read_text() for path in sorted((ROOT / ".github/workflows").glob("*.yml"))
    )
    failures.require(
        "ubuntu-latest" not in workflow and workflow.count("runs-on: ubuntu-24.04") == 3,
        "CI jobs must use the explicit ubuntu-24.04 runner",
    )
    action_references = re.findall(r"^\s*-?\s*uses:\s*([^\s#]+)", workflow, re.MULTILINE)
    failures.require(
        bool(action_references)
        and all(re.search(r"@[0-9a-f]{40}$", reference) for reference in action_references),
        "every GitHub Action must be pinned to an immutable 40-character commit SHA",
    )

    ota = (
        ROOT / "components/smoker_platform/src/firmware_update_service.cpp"
    ).read_text()
    ota_header = (
        ROOT / "components/smoker_platform/include/smoker/platform/firmware_update_support.hpp"
    ).read_text()
    ota_impl = source_section(
        ota,
        "class FirmwareUpdateService::Impl final {",
        "FirmwareUpdateService::FirmwareUpdateService(",
    )
    release = (ROOT / ".github/workflows/release.yml").read_text()
    firmware_check_handler = source_section(
        connectivity,
        "esp_err_t handle_firmware_check(",
        "esp_err_t handle_firmware_install(",
    )
    firmware_check_admission = firmware_check_handler.find(
        "firmware_updates_.request_check()"
    )
    firmware_check_send = firmware_check_handler.find("httpd_resp_send(")
    failures.require(
        "firmware_check_accepted_body" in firmware_check_handler
        and "CjsonPointer" not in firmware_check_handler
        and firmware_check_admission >= 0
        and firmware_check_send > firmware_check_admission,
        "firmware check must use a prebuilt fixed response without post-admission allocation",
    )
    failures.require(
        runtime.count("rollback_pending_firmware_and_reboot_if_needed();") == 2
        and "Could not allocate simulation context" in runtime
        and "Could not create ControlTask" in runtime,
        "pending firmware must roll back when critical runtime bootstrap fails",
    )
    failures.require(
        'coordinator_.fail("ota_task_unavailable")' in ota
        and ota.count("if (!worker_available())") >= 2,
        "an unavailable OtaTask must fail status and reject check/install requests",
    )
    failures.require(
        "DRAM_ATTR StaticTask_t ota_task_storage" in ota
        and "DRAM_ATTR std::array<StackType_t" in ota
        and "ota_task_stack.data()" in ota
        and "&ota_task_storage" in ota
        and re.search(r"\btask_stack_\b", ota_impl) is None
        and re.search(r"\btask_storage_\b", ota_impl) is None,
        "OtaTask stack and TCB must stay in internal DRAM, outside the heap-owned service",
    )
    failures.require(
        '"OtaTask"' in ota
        and re.search(
            r'xTaskCreateStaticPinnedToCore\(.*?"OtaTask".*?\n\s*0\s*\n\s*\);',
            ota,
            re.DOTALL,
        ) is not None
        and "esp_task_wdt_add" not in ota,
        "the single low-priority OtaTask must be static, core-0 pinned, and outside TWDT",
    )
    failures.require(
        "esp_http_client_open" in ota
        and "esp_http_client_fetch_headers" in ota
        and "esp_http_client_read" in ota
        and "esp_crt_bundle_attach" in ota
        and "pool.ntp.org" in ota
        and "HTTP_TRANSPORT_OVER_SSL" in ota
        and "disable_auto_redirect = true" in ota
        and "esp_http_client_set_redirection" in ota
        and "maximum_redirects" in ota
        and "configuration.buffer_size_tx" in ota
        and "maximum_http_request_line_overhead" in ota
        and "std::numeric_limits<int>::max()" in ota
        and "MonotonicDeadline" in ota
        and "permission_timeout_microseconds" in ota
        and "check_timeout_microseconds" in ota
        and "install_timeout_microseconds" in ota
        and "esp_ota_begin" in ota
        and "esp_ota_write" in ota
        and "esp_ota_end" in ota
        and "esp_ota_set_boot_partition" in ota
        and "esp_ota_mark_app_valid_cancel_rollback" in ota
        and "esp_ota_mark_app_invalid_rollback_and_reboot" in ota,
        "M13 bounded HTTPS download, OTA write, validation, or rollback calls are missing",
    )
    failures.require(
        "esp_https_ota_" not in ota
        and "firmware_target_chip_id" in ota_header
        and "parse_firmware_image_metadata" in ota_header,
        "M13 must parse the real image chip ID and must not use unbounded esp_https_ota helpers",
    )
    normalized_ota_header = ota_header.replace('"\n    "', "")
    failures.require(
        "firmware_release_url" in ota_header
        and "releases/latest/download/smoker_controller.bin" in normalized_ota_header
        and "http://" not in ota_header,
        "M13 firmware source must remain the one fixed HTTPS GitHub release asset",
    )
    failures.require(
        connectivity.count('std::pair{"/api/v1/firmware"') == 1
        and connectivity.count('std::pair{"/api/v1/firmware/check"') == 1
        and connectivity.count('std::pair{"/api/v1/firmware/install"') == 1,
        "M13 authenticated firmware routes are missing",
    )
    prepare_consume = runtime.find("consume_prepare_request(")
    finish_consume = runtime.find("consume_finish_request()")
    failures.require(
        "PrepareFirmwareUpdateCommand" not in connectivity
        and "prepare_correlation_.compare_exchange_strong(" in ota
        and prepare_consume >= 0
        and finish_consume > prepare_consume
        and "retry_prepare_request(" in runtime
        and "!prepare_submission_pending" in runtime,
        "OTA Prepare must use its bounded ControlTask signal and Finish must not overtake it",
    )
    failures.require(
        'tags:\n      - "v*.*.*"' in release
        and 'test "$GITHUB_REF_NAME" = "v$version"' in release
        and "environment: firmware-release" in release
        and "secrets.SMOKER_OTA_SIGNING_KEY_B64" in release
        and "extra_docker_args: --env SMOKER_OTA_SIGNING_KEY_B64" in release
        and "tools/sign_release_firmware.sh" in release
        and "cp build-verify/smoker_controller.bin" not in release
        and "gh release create" in release
        and "smoker_controller.bin.sha256" in release,
        "M13 release must sign in a tag-restricted environment and publish only the verified image",
    )


def check_m14_history_contract(failures: CheckFailures) -> None:
    runtime = (ROOT / "components/smoker_platform/src/simulation_runtime.cpp").read_text()
    history = (ROOT / "components/smoker_platform/src/history_service.cpp").read_text()
    support = (ROOT / "components/smoker_platform/src/history_support.cpp").read_text()
    header = (ROOT / "components/smoker_platform/include/smoker/platform/history_support.hpp").read_text()
    coordinator = (ROOT / "components/smoker_platform/include/smoker/platform/flash_operation_coordinator.hpp").read_text()
    ota = (ROOT / "components/smoker_platform/src/firmware_update_service.cpp").read_text()
    connectivity = (ROOT / "components/smoker_platform/src/local_connectivity.cpp").read_text()
    network_support = (ROOT / "components/smoker_platform/src/local_network_support.cpp").read_text()
    web_assets = (ROOT / "components/smoker_platform/src/web_assets.hpp").read_text()
    tests = (ROOT / "tests/host/smoker_m14_tests.cpp").read_text()
    partitions = (ROOT / "partitions.csv").read_text()

    tick = runtime.find("context->application.tick();")
    publish = runtime.find("context->snapshots.publish(snapshot)", tick)
    observe = runtime.find("context->history_mailbox.observe(snapshot)", publish)
    failures.require(
        tick >= 0 and publish > tick and observe > publish,
        "ControlTask must publish history only after the safety-gated tick and snapshot",
    )
    control_loop = source_section(runtime, "void control_task(", "bool start_simulation_runtime(")
    failures.require(
        bool(control_loop)
        and "history_mailbox.observe(snapshot)" in control_loop
        and "synchronized_unix_utc_now" not in control_loop
        and "esp_partition_" not in control_loop
        and "FlashOperationCoordinator" not in control_loop
        and "std::mutex" not in control_loop,
        "ControlTask history publication must not perform flash I/O or synchronization",
    )
    failures.require(
        "history_mailbox_capacity = 16U" in header
        and "std::array<Slot, history_mailbox_capacity>" in header
        and "std::atomic<std::uint32_t> write_sequence_" in header
        and "std::atomic<std::uint32_t> read_sequence_" in header
        and "std::atomic<std::uint64_t>" not in header,
        "history transport must remain a preallocated native-atomic 16-entry SPSC mailbox",
    )
    failures.require(
        "history_periodic_sample_interval{60'000}" in header
        and "history_change_coalesce_interval" not in header
        and "HistoryObservationKind::Start" in support
        and "HistoryObservationKind::Sample" in support
        and "HistoryObservationKind::Change" in support
        and "HistoryObservationKind::End" in support,
        "M14 start/sample/change/end observation policy is missing",
    )
    failures.require(
        "DRAM_ATTR StaticTask_t history_task_storage" in history
        and "DRAM_ATTR std::array<StackType_t" in history
        and '"HistoryTask"' in history
        and re.search(
            r'xTaskCreateStaticPinnedToCore\(.*?"HistoryTask".*?\n\s*0\s*\n\s*\);',
            history,
            re.DOTALL,
        ) is not None
        and "esp_task_wdt_add" not in history,
        "HistoryTask must use static internal DRAM, core 0, low priority, and stay outside TWDT",
    )
    failures.require(
        "esp_partition_write" in history
        and "esp_partition_erase_range" in history
        and "esp_partition_write" not in support
        and "esp_partition_erase_range" not in support,
        "only the HistoryTask adapter may own raw history-partition flash APIs",
    )
    failures.require(
        "commit_marker" in support and "eviction_marker" in support and "crc32(" in support
        and "next_generation_" in header and "erase_sector" in support
        and "choose_oldest(true, true)" in support
        and "complete_pending_eviction" in support
        and "summary.truncated" in support,
        "raw history recovery, commit-last, eviction, and truncation contracts are missing",
    )
    failures.require(
        "try_acquire_history" in coordinator and "try_acquire_ota" in coordinator
        and "history_deferred_" in coordinator
        and "OtaFlashLease" in ota and "HistoryFlashLease" in history
        and "deadline.expired(esp_timer_get_time())" in ota,
        "history and OTA flash operations must share the platform-owned coordinator",
    )
    failures.require(
        "}\n            static_cast<void>(ulTaskNotifyTake" in history,
        "HistoryTask must release its flash lease before its idle wait",
    )
    failures.require(
        "std::optional<HistoryObservation> pending_lifecycle" in history
        and "observation.kind == HistoryObservationKind::Start" in history
        and "observation.kind == HistoryObservationKind::End" in history
        and "pending_lifecycle = observation" in history,
        "HistoryTask must retain failed START/END lifecycle records for durable retry",
    )
    failures.require(
        re.search(
            r"^history,\s*data,\s*0x40,\s*0x620000,\s*0x400000,?\s*$",
            partitions,
            re.MULTILINE,
        ) is not None,
        "M14 must retain the exact 4 MiB history partition at 0x620000",
    )
    failures.require(
        connectivity.count('std::pair{"/api/v1/history/sessions", HTTP_GET}') == 1
        and connectivity.count('std::pair{"/api/v1/history/samples", HTTP_GET}') == 1
        and "parse_history_sessions_query" in network_support
        and "parse_history_samples_query" in network_support
        and "httpd_resp_send_chunk" in connectivity,
        "authenticated read-only history routes must use strict parsers and chunked JSON",
    )
    failures.require(
        'id="history-chart"' in web_assets and "refreshHistory" in web_assets
        and "loadHistorySamples" in web_assets
        and "const historyPointBudget = 1200" in web_assets
        and "while (after !== null)" in web_assets
        and "item.kind === 'SAMPLE'" in web_assets
        and "item.kind === 'CHANGE'" in web_assets
        and "historyLoadToken" in web_assets
        and "https://" not in web_assets and "http://" not in web_assets,
        "the bounded embedded history dashboard contract is missing",
    )
    for evidence in (
        "test_empty_random_and_reboot",
        "test_torn_and_corrupt_records",
        "test_pagination_and_stride",
        "test_rollover_eviction_truncation_and_interruption",
        "test_sampling_and_mailbox_saturation",
        "test_flash_operation_serialization",
        "test_mailbox_concurrency",
        "test_strict_history_queries",
    ):
        failures.require(evidence in tests, f"M14 host evidence is missing: {evidence}")


def main() -> int:
    failures = CheckFailures()
    check_layer_includes(failures)
    check_control_ownership(failures)
    check_component_graph(failures)
    check_v0_scope(failures)
    check_reproducible_build_contract(failures)
    check_m12_transport_contract(failures)
    check_m14_history_contract(failures)
    return failures.finish()


if __name__ == "__main__":
    raise SystemExit(main())

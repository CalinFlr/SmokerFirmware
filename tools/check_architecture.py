#!/usr/bin/env python3
"""Fail when the M0-M15 source tree violates approved architecture boundaries."""

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
    expected_blynk = "components/smoker_platform/src/blynk_service.cpp"
    expected_history_support = "components/smoker_platform/src/history_support.cpp"
    expected_application = "components/smoker_app/src/smoker_application.cpp"

    task_calls = find_calls(r"\bxTaskCreate(?:Static)?(?:PinnedToCore)?\s*\(", production)
    failures.require(
        len(task_calls) == 5,
        "M15 must contain only ControlTask, OtaTask, HistoryTask, BlynkTask, and the captive DNS helper task; "
        f"found {len(task_calls)} task-creation calls",
    )
    task_owners = {relative(path) for path, _ in task_calls}
    failures.require(
        task_owners == {
            expected_runtime, expected_connectivity, expected_ota,
            expected_history, expected_blynk,
        },
        "task creation must be limited to ControlTask, OtaTask, HistoryTask, BlynkTask, and captive DNS; "
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

    submit_calls = find_calls(r"(?:\.|->)\s*submit\s*\(", production)
    failures.require(
        len(submit_calls) == 1,
        f"M15 production must have one ControlTask-owned submit call site; found {len(submit_calls)}",
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
        "external_commands.drain(" in runtime
        and "context->http_mailbox" in runtime
        and "context->blynk_mailbox" in runtime
        and "submit_to_application" in runtime,
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
        decision_ids == list(range(1, 59)),
        f"decision IDs must remain ordered and contiguous through D058; found {decision_ids}",
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
        and "Production continues to compose `SimulatedFoodProbeSource`" in decisions
        and "injected calibration/validity policy" in decisions
        and "CONFIG_I2CDEV_TIMEOUT" in decisions
        and "no ADS1115 value or failure directly changes" in decisions,
        "D057 must preserve the exact-pinned dual-ADS1115 dependency, inactive sequencer, physical gate, and monitoring-only boundary",
    )
    failures.require(
        "## D058 — M15 pins ESP-MQTT and provisions Blynk through UART0/NVS"
        in decisions
        and "espressif/mqtt" in decisions
        and "ffdad5659706b4dc14bc63f8eb73ef765efa015bf7e9adf71c813d52a2dc9342"
        in decisions
        and "FUMURI-BLYNK/1" in decisions
        and "unencrypted NVS" in decisions
        and "second SPSC mailbox" in decisions,
        "D058 must preserve the exact MQTT pin, UART/NVS risk, and two-mailbox boundary",
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


def check_m7_max31865_contract(failures: CheckFailures) -> None:
    platform = ROOT / "components/smoker_platform"
    sensor_header = (
        platform / "include/smoker/platform/max31865_sensor.hpp"
    ).read_text()
    target_header = (
        platform / "include/smoker/platform/max31865_target_backend.hpp"
    ).read_text()
    sensor = (platform / "src/max31865_sensor.cpp").read_text()
    target = (platform / "src/max31865_target_backend.cpp").read_text()
    board_pins = (
        platform / "include/smoker/platform/max31865_board_pins.hpp"
    ).read_text()
    diagnostic_header = (
        platform / "include/smoker/platform/max31865_connected_diagnostic.hpp"
    ).read_text()
    diagnostic = (
        platform / "src/max31865_connected_diagnostic.cpp"
    ).read_text()
    platform_cmake = (platform / "CMakeLists.txt").read_text()
    runtime = (platform / "src/simulation_runtime.cpp").read_text()
    main_source = (ROOT / "main/app_main.cpp").read_text()
    tests = (ROOT / "tests/host/smoker_m7_tests.cpp").read_text()
    diagnostic_kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
    diagnostic_defaults = (
        ROOT / "diagnostics/max31865/sdkconfig.defaults"
    ).read_text()

    lower_layer_text = "\n".join(
        path.read_text()
        for path in source_files("components/smoker_core", "components/smoker_app")
    )
    failures.require(
        "max31865" not in lower_layer_text.lower()
        and "driver/spi" not in lower_layer_text.lower()
        and "driver/gpio" not in lower_layer_text.lower(),
        "smoker_core/smoker_app must not contain MAX31865, SPI, or GPIO dependencies",
    )

    failures.require(
        '"src/max31865_sensor.cpp"' in platform_cmake
        and '"src/max31865_target_backend.cpp"' in platform_cmake
        and platform_cmake.find('"src/max31865_target_backend.cpp"')
            > platform_cmake.find("if(ESP_PLATFORM)"),
        "the MAX31865 policy must be host-buildable and its real backend target-only",
    )
    sensor_read = source_section(
        sensor,
        "std::optional<core::Temperature> Max31865ChamberSensor::read()",
        "bool Max31865ChamberSensor::configured()",
    )
    failures.require(
        "class Max31865ChamberSensor final : public app::IChamberSensor"
            in sensor_header
        and "IMax31865Backend" in sensor_header
        and "ConfiguredAwaitingFirstSample" in sensor_header
        and "NotReady" in sensor_header
        and bool(sensor_read)
        and "result.status != Max31865ReadStatus::Valid" in sensor_read
        and "std::isfinite(result.celsius)" in sensor_read
        and "last" not in sensor_read.lower(),
        "the MAX31865 chamber adapter must map only each current finite valid sample",
    )
    failures.require(
        "reference_resistance_ohms;" in sensor_header
        and "Max31865FilterFrequency filter;" in sensor_header
        and "Max31865RtdStandard standard;" in sensor_header
        and "spi_host_device_t spi_host;" in target_header
        and "gpio_num_t chip_select_gpio;" in target_header
        and "std::uint32_t clock_speed_hz;" in target_header,
        "unknown MAX31865 hardware values must remain explicit required configuration",
    )

    for api in (
        "max31865_init_desc(",
        "max31865_set_config(",
        "max31865_get_fault_status(",
        "max31865_clear_fault_status(",
        "max31865_read_temperature(",
        "max31865_free_desc(",
    ):
        failures.require(api in target, f"the target MAX31865 backend must call {api}")
    failures.require(
        "MAX31865_MODE_AUTO" in target
        and "MAX31865_3WIRE" in target
        and "max31865_pt100_nominal_ohms" in target
        and "~Max31865TargetBackend()" in target
        and "release_descriptor();" in target,
        "the inactive target backend must use provisional continuous PT100/3-wire RAII",
    )

    production_text = "\n".join(
        path.read_text() for path in source_files("components", "main")
    )
    read_section = source_section(
        target,
        "Max31865ReadResult Max31865TargetBackend::read_continuous()",
        "void Max31865TargetBackend::release_descriptor()",
    )
    failures.require(bool(read_section), "the target MAX31865 read boundary is missing")
    readiness_check = read_section.find("if (!readiness_.sample_ready())")
    first_fault_read = read_section.find("max31865_get_fault_status(")
    temperature_read = read_section.find("max31865_read_temperature(")
    failures.require(
        readiness_check >= 0
        and first_fault_read > readiness_check
        and temperature_read > readiness_check,
        "MAX31865 sample readiness must be checked before fault/temperature register reads",
    )
    configure_section = source_section(
        target,
        "bool Max31865TargetBackend::configure_device()",
        "void Max31865TargetBackend::clear_fault_for_later_read()",
    )
    set_config = configure_section.find("max31865_set_config(")
    readiness_reset = configure_section.find(
        "readiness_.continuous_configuration_applied()"
    )
    failures.require(
        bool(configure_section)
        and "readiness_.invalidate()" in configure_section
        and set_config >= 0
        and readiness_reset > set_config,
        "every successful continuous MAX31865 configuration must reset sample readiness",
    )
    initialize_section = source_section(
        target,
        "Max31865InitializationStatus Max31865TargetBackend::initialize()",
        "Max31865ReadResult Max31865TargetBackend::read_continuous()",
    )
    failures.require(
        "release_descriptor();" in initialize_section
        and "if (configured_)" not in initialize_section,
        "MAX31865 reinitialization must conservatively replace configuration and readiness",
    )
    recovery_section = source_section(
        target,
        "void Max31865TargetBackend::clear_fault_for_later_read()",
        "bool Max31865TargetBackend::valid_configuration()",
    )
    recovery_invalidate = recovery_section.find("readiness_.invalidate()")
    fault_clear = recovery_section.find("max31865_clear_fault_status(")
    recovery_configure = recovery_section.find("configure_device()")
    failures.require(
        recovery_invalidate >= 0
        and fault_clear > recovery_invalidate
        and recovery_configure > fault_clear,
        "MAX31865 fault recovery must invalidate readiness before clear/reconfiguration",
    )
    for forbidden in (
        "max31865_measure(",
        "vTaskDelay(",
        "sleep(",
        "delay(",
        "new ",
        "malloc(",
        "calloc(",
        "realloc(",
        "make_unique",
        "make_shared",
        "xTaskCreate",
    ):
        failures.require(
            forbidden not in target,
            f"MAX31865 critical read must not contain {forbidden}",
        )
    failures.require(
        "max31865_measure(" not in production_text,
        "max31865_measure() must not enter project-owned production sources",
    )
    failures.require(
        "xTaskCreate" not in sensor and "xTaskCreate" not in target,
        "the MAX31865 adapter/backend must not create a task",
    )
    failures.require(
        "SimulatedChamberSensor" in runtime
        and "start_simulation_runtime" in main_source
        and "Max31865TargetBackend" not in runtime
        and "Max31865TargetBackend" not in main_source,
        "production composition must remain on SimulatedChamberSensor",
    )

    failures.require(
        "max31865_spi_host = SPI2_HOST" in board_pins
        and "max31865_sck_gpio = GPIO_NUM_12" in board_pins
        and "max31865_mosi_gpio = GPIO_NUM_11" in board_pins
        and "max31865_miso_gpio = GPIO_NUM_13" in board_pins
        and "max31865_chip_select_gpio = GPIO_NUM_10" in board_pins
        and "max31865_board_pins.hpp" in diagnostic
        and "max31865_board_pins.hpp" in target
        and "configuration_.spi_host == max31865_spi_host" in target
        and "configuration_.chip_select_gpio == max31865_chip_select_gpio"
            in target
        and "GPIO_NUM_12" not in diagnostic
        and "GPIO_NUM_11" not in diagnostic
        and "GPIO_NUM_13" not in diagnostic
        and "GPIO_NUM_10" not in diagnostic,
        "the final soldered MAX31865 SPI2/GPIO12/11/13/10 assignment must be centralized",
    )
    failures.require(
        "config SMOKER_MAX31865_CONNECTED_DIAGNOSTIC" in diagnostic_kconfig
        and "default n" in diagnostic_kconfig
        and "CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC=y"
            in diagnostic_defaults
        and "if(CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC)"
            in platform_cmake
        and '"src/max31865_connected_diagnostic.cpp"' in platform_cmake
        and platform_cmake.find('"src/max31865_connected_diagnostic.cpp"')
            > platform_cmake.find(
                "if(CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC)"
            )
        and '#include "sdkconfig.h"' in main_source
        and "#ifdef CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC"
            in main_source
        and "#else" in main_source
        and "run_max31865_connected_diagnostic()" in main_source
        and "run_max31865_connected_diagnostic()" in diagnostic_header,
        "the MAX31865 diagnostic must remain explicit, default-OFF, target-only, and composition-exclusive",
    )
    failures.require(
        "GPIO_PULLUP_ONLY" in diagnostic
        and "GPIO_PULLDOWN_ONLY" in diagnostic
        and "stable_configuration_bits_mask = 0xD1U" in diagnostic
        and "idle_2wire_60hz_pattern = 0x00U" in diagnostic
        and "idle_bias_3wire_50hz_pattern = 0x91U" in diagnostic
        and "active_auto_3wire_50hz_pattern = 0xD1U" in diagnostic
        and "terminal_quiescent_configuration_pattern = 0x11U"
            in diagnostic
        and "sample_count = 10U" in diagnostic
        and "~SoftwareSpiPinsOwner()" in diagnostic
        and "~SpiBusOwner()" in diagnostic
        and "~Max31865DeviceOwner()" in diagnostic
        and "reset_diagnostic_pins();" in diagnostic,
        "the board diagnostic must reject pull-following MISO, use bounded persistent patterns, and retain RAII ownership",
    )
    software_active = diagnostic.find(
        '"active sampling configuration",\n'
        "            active_auto_3wire_50hz_pattern"
    )
    software_shutdown = diagnostic.find("if (!pins.quiesce_checked())")
    driver_shutdown = diagnostic.find(
        "const bool shutdown_ok = device_owner.quiesce_checked();"
    )
    descriptor_release = diagnostic.find("max31865_free_desc(&device_)")
    bus_release = diagnostic.find("spi_bus_free(max31865_spi_host)")
    device_owner = diagnostic.find("Max31865DeviceOwner device_owner;")
    failures.require(
        "software_write_and_verify_exact_config(" in diagnostic
        and "quiesce_software_spi_converter(" in diagnostic
        and "software_spi_force_idle_frame_boundary()" in diagnostic
        and "cleanup frame boundary" in diagnostic
        and "if (quiesced_) return true;" in diagnostic
        and software_active >= 0
        and software_shutdown > software_active
        and "software-SPI destructor fallback quiescence" in diagnostic,
        "software SPI must finish with checked exact command-zero quiescence and retain a destructor fallback",
    )
    failures.require(
        "driver_write_exact_config(" in diagnostic
        and "driver_read_exact_config(" in diagnostic
        and "quiesce_driver_converter(" in diagnostic
        and "max31865_set_config() RMW preservation" in diagnostic
        and "D5, D3:D2, and D1" in diagnostic
        and driver_shutdown > device_owner >= 0
        and 'return transaction_errors == 0U && shutdown_ok;' in diagnostic
        and "MAX31865 descriptor destructor fallback quiescence" in diagnostic,
        "the driver path must use checked exact quiescence, fail on shutdown error, and retain an early-return fallback",
    )
    failures.require(
        descriptor_release >= 0
        and bus_release >= 0
        and device_owner > bus_release
        and "MAX31865 descriptor release failed" in diagnostic
        and "SPI2 release failed" in diagnostic,
        "descriptor removal must remain attempted before local SPI-bus release even when quiescence fails",
    )
    failures.require(
        "sensor fault samples are distinct from SPI transaction or shutdown failure"
            in diagnostic
        and "RTD health" in diagnostic,
        "sensor fault observations must remain distinct from communication/shutdown success",
    )
    for forbidden in (
        "SmokerApplication",
        "start_simulation_runtime",
        "IHeaterOutput",
        "max31865_read_temperature(",
        "max31865_measure(",
        "max31865_detect_fault_auto(",
        "xTaskCreate",
        "for (;;)",
        "while (true)",
    ):
        failures.require(
            forbidden not in diagnostic,
            f"the isolated MAX31865 diagnostic must not contain {forbidden}",
        )

    for evidence in (
        "test_max31865_configuration_policy_requires_explicit_valid_values",
        "test_max31865_60_hz_first_conversion_boundary",
        "test_max31865_50_hz_first_conversion_boundary",
        "test_max31865_initialization_and_configuration_failures_are_absent",
        "test_max31865_read_policy_never_reuses_a_previous_value",
        "test_max31865_por_value_is_not_exposed_before_readiness",
        "test_max31865_reconfiguration_resets_readiness_without_reuse",
        "test_max31865_reinitialization_resets_readiness",
        "test_max31865_fault_recovery_requires_fresh_current_value",
        "test_max31865_read_is_observed_allocation_free",
        "test_max31865_premature_application_tick_latches_fault_and_heater_off",
    ):
        failures.require(evidence in tests, f"M7 host evidence is missing: {evidence}")


def check_m9_ads1115_contract(failures: CheckFailures) -> None:
    platform = ROOT / "components/smoker_platform"
    source_header = (
        platform / "include/smoker/platform/ads1115_food_probe_source.hpp"
    ).read_text()
    target_header = (
        platform / "include/smoker/platform/ads1115_target_backend.hpp"
    ).read_text()
    source = (platform / "src/ads1115_food_probe_source.cpp").read_text()
    target = (platform / "src/ads1115_target_backend.cpp").read_text()
    platform_cmake = (platform / "CMakeLists.txt").read_text()
    runtime = (platform / "src/simulation_runtime.cpp").read_text()
    main_source = (ROOT / "main/app_main.cpp").read_text()
    tests = (ROOT / "tests/host/smoker_m9_tests.cpp").read_text()

    lower_layer_text = "\n".join(
        path.read_text()
        for path in source_files("components/smoker_core", "components/smoker_app")
    )
    failures.require(
        "ads111" not in lower_layer_text.lower()
        and "i2c_dev_t" not in lower_layer_text
        and "driver/i2c" not in lower_layer_text.lower(),
        "smoker_core/smoker_app must not contain ADS1115, i2cdev, or I2C driver types",
    )
    failures.require(
        '"src/ads1115_food_probe_source.cpp"' in platform_cmake
        and '"src/ads1115_target_backend.cpp"' in platform_cmake
        and platform_cmake.find('"src/ads1115_target_backend.cpp"')
            > platform_cmake.find("if(ESP_PLATFORM)"),
        "the ADS1115 sequencer must be host-buildable and its real backend target-only",
    )

    failures.require(
        "class Ads1115FoodProbeSource final : public app::IFoodProbeSource"
            in source_header
        and "class IAds1115Backend" in source_header
        and "class IAds1115SampleConverter" in source_header
        and "void service() noexcept;" in source_header
        and "std::vector<std::optional<CachedSample>> samples_" in source_header
        and "AcquisitionState state_" in source_header,
        "M9 must retain one host-testable acquisition owner with timestamped caches",
    )
    failures.require(
        all(
            required in source_header
            for required in (
                "int i2c_port;",
                "int sda_gpio;",
                "int scl_gpio;",
                "std::uint32_t clock_speed_hz;",
                "Ads1115PullupPolicy pullup_policy;",
                "std::uint8_t address;",
                "std::size_t device_index;",
                "Ads1115Mux mux;",
                "Ads1115Gain gain;",
                "Ads1115DataRate data_rate;",
                "core::Duration conversion_timeout_",
                "core::Duration sample_maximum_age_",
            )
        )
        and "Ads1115DeviceConfiguration()" not in source_header
        and "Ads1115ChannelConfiguration()" not in source_header
        and "Ads1115AcquisitionConfiguration()" not in source_header,
        "all ADS1115 bus, mapping, conversion, timeout, and age values must be explicit",
    )
    failures.require(
        "devices.size() != ads1115_device_count" in source
        and "compatible_device_pair" in source
        and "first.address != second.address" in source
        and "first.i2c_port == second.i2c_port" in source
        and "device_has_channel" in source
        and "channels[earlier].probe_id == channel.probe_id" in source
        and "minimum_ads1115_conversion_timeout(channel.data_rate)" in source,
        "M9 configuration must validate two devices, shared buses, mappings, IDs, and deadlines",
    )

    read_section = source_section(
        source,
        "std::optional<core::Temperature> Ads1115FoodProbeSource::read(",
        "bool Ads1115FoodProbeSource::configured()",
    )
    failures.require(
        bool(read_section)
        and "backend_" not in read_section
        and "samples_[index]" in read_section
        and "sample_maximum_age()" in read_section
        and "return std::nullopt" in read_section,
        "IFoodProbeSource::read() must perform cached age validation without I2C",
    )
    start_section = source_section(
        source,
        "void Ads1115FoodProbeSource::start_next_conversion()",
        "void Ads1115FoodProbeSource::poll_active_conversion()",
    )
    poll_section = source_section(
        source,
        "void Ads1115FoodProbeSource::poll_active_conversion()",
        "void Ads1115FoodProbeSource::fail_active_probe()",
    )
    failures.require(
        bool(start_section)
        and "backend_.configure_channel(channel)" in start_section
        and "backend_.start_conversion(channel.device_index)" in start_section
        and "backend_.get_value" not in start_section
        and bool(poll_section)
        and poll_section.find("clock_.now() >= conversion_deadline_")
            < poll_section.find("backend_.conversion_busy(")
        and poll_section.find("backend_.conversion_busy(")
            < poll_section.find("if (busy) return")
            < poll_section.find("backend_.get_value(")
        and poll_section.find("backend_.get_value(")
            < poll_section.find("sample_converter_.convert("),
        "M9 must start and later poll/read/calibrate the same explicit channel in order",
    )
    failures.require(
        "samples_[active_channel_].reset();" in source
        and "next_channel_ = (active_channel_ + 1U)" in source,
        "M9 failures must clear only the active probe and advance one owner sequence",
    )

    for api in (
        "ads111x_init_desc(",
        "ads111x_free_desc(",
        "ads111x_set_mode(",
        "ads111x_set_input_mux(",
        "ads111x_set_gain(",
        "ads111x_set_data_rate(",
        "ads111x_start_conversion(",
        "ads111x_is_busy(",
        "ads111x_get_value(",
    ):
        failures.require(api in target, f"the target ADS1115 backend must call {api}")
    initialize_section = source_section(
        target,
        "bool Ads1115TargetBackend::initialize(",
        "bool Ads1115TargetBackend::configure_channel(",
    )
    init_desc = initialize_section.find("ads111x_init_desc(")
    clock_override = initialize_section.find("descriptor.cfg.master.clk_speed")
    first_io = initialize_section.find("ads111x_set_mode(")
    failures.require(
        bool(initialize_section)
        and init_desc >= 0
        and clock_override > init_desc
        and first_io > clock_override
        and "ADS111X_MODE_SINGLE_SHOT" in initialize_section
        and "descriptor.cfg.sda_pullup_en" in initialize_section
        and "descriptor.cfg.scl_pullup_en" in initialize_section,
        "the explicit clock/pull-up policy must override init_desc before first single-shot I2C I/O",
    )
    failures.require(
        "std::array<i2c_dev_t, 2U> descriptors_" in target_header
        and "~Ads1115TargetBackend()" in target
        and "release_descriptors();" in target,
        "the target backend must be the RAII owner of both ADS1115 descriptors",
    )

    project_m9 = source + "\n" + target
    for forbidden in (
        "vTaskDelay(",
        "sleep(",
        "delay(",
        "malloc(",
        "calloc(",
        "realloc(",
        "new ",
        "make_unique",
        "make_shared",
        "xTaskCreate",
        "i2cdev_init(",
    ):
        failures.require(
            forbidden not in project_m9,
            f"project-owned ADS1115 paths must not contain {forbidden}",
        )
    failures.require(
        "ads111x_gain_values" not in project_m9
        and "raw_to_voltage" not in project_m9.lower()
        and "steinhart" not in project_m9.lower(),
        "the ADS1115 adapter must expose raw codes only to injected calibration",
    )
    failures.require(
        "SimulatedFoodProbeSource" in runtime
        and "start_simulation_runtime" in main_source
        and "Ads1115FoodProbeSource" not in runtime
        and "Ads1115TargetBackend" not in runtime
        and "Ads1115FoodProbeSource" not in main_source
        and "Ads1115TargetBackend" not in main_source
        and "i2cdev_init(" not in runtime
        and "i2cdev_init(" not in main_source,
        "production composition must remain simulated and must not initialize ADS1115/I2C",
    )

    for evidence in (
        "test_ads1115_invalid_incomplete_configurations_are_rejected",
        "test_ads1115_two_devices_and_channels_are_sequenced_without_reuse",
        "test_ads1115_start_and_read_never_share_a_service_step",
        "test_ads1115_busy_and_stuck_conversion_never_read_or_block",
        "test_ads1115_mux_gain_rate_changes_require_a_new_completed_conversion",
        "test_ads1115_per_probe_failures_clear_only_the_affected_sample",
        "test_ads1115_cached_readings_expire_and_unknown_ids_are_absent",
        "test_ads1115_steady_state_service_and_read_are_observed_allocation_free",
        "test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control",
    ):
        failures.require(evidence in tests, f"M9 host evidence is missing: {evidence}")


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


def check_m15_blynk_contract(failures: CheckFailures) -> None:
    platform = ROOT / "components/smoker_platform"
    service = (platform / "src/blynk_service.cpp").read_text()
    connection = (platform / "src/blynk_connection_support.cpp").read_text()
    connection_header = (
        platform / "include/smoker/platform/blynk_connection_support.hpp"
    ).read_text()
    commands = (platform / "src/blynk_command_support.cpp").read_text()
    command_header = (
        platform / "include/smoker/platform/blynk_command_support.hpp"
    ).read_text()
    remote = (platform / "src/blynk_remote_support.cpp").read_text()
    remote_header = (
        platform / "include/smoker/platform/blynk_remote_support.hpp"
    ).read_text()
    provisioning = (platform / "src/blynk_provisioning_support.cpp").read_text()
    runtime_support = (platform / "src/runtime_transport_support.cpp").read_text()
    runtime_header = (
        platform / "include/smoker/platform/runtime_transport_support.hpp"
    ).read_text()
    runtime = (platform / "src/simulation_runtime.cpp").read_text()
    manifest = (platform / "idf_component.yml").read_text()
    lock = (ROOT / "dependencies.lock").read_text()
    defaults = (ROOT / "sdkconfig.defaults").read_text()
    tool = (ROOT / "tools/provision_blynk.py").read_text()
    tests = (ROOT / "tests/host/smoker_m15_tests.cpp").read_text()

    non_platform = "\n".join(
        path.read_text()
        for path in source_files("components/smoker_core", "components/smoker_app", "main")
    )
    failures.require(
        "blynk" not in non_platform.lower() and "mqtt_client" not in non_platform,
        "Blynk and MQTT implementation identifiers must remain confined to smoker_platform",
    )
    failures.require(
        'espressif/mqtt: "==1.0.0"' in manifest
        and "espressif/mqtt:" in lock
        and "version: 1.0.0" in lock
        and "ffdad5659706b4dc14bc63f8eb73ef765efa015bf7e9adf71c813d52a2dc9342"
        in lock,
        "M15 must exact-pin and lock official espressif/mqtt 1.0.0",
    )

    callback = source_section(service, "    void mqtt_event(", "    void run()")
    failures.require(
        bool(callback)
        and "inbound_.push(" in callback
        and "connection_boundary_.callback_connected()" in callback
        and "connection_boundary_.callback_disconnected()" in callback
        and "is_blynk_control_datastream" not in callback
        and "submit(" not in callback
        and "heater" not in callback.lower(),
        "the MQTT callback must only perform connection bookkeeping and bounded raw-mailbox copy",
    )
    failures.require(
        "disconnect_generation_" in connection_header
        and "connection_generation_" in connection_header
        and "result.cleanup_required" in connection
        and "result.connection_started" in connection
        and "if (connection.cleanup_required) handle_disconnect();\n"
            "            if (connection.connection_started) projection_.connected();"
            in service
        and "disconnect_inbound_drops_.store(" in service
        and "observed_inbound_drops_ = disconnect_inbound_drops_.load(" in service
        and "pending_feedback_.reset();" in service
        and "inbound.connection_generation != connection.connection_generation"
            in service
        and "connection.connection_generation\n                );" in service
        and "validate_blynk_generation" in runtime,
        "M15 disconnect cleanup and connection-generation integration are incomplete",
    )
    failures.require(
        "transport_generation" in runtime_support
        and "validate_blynk_generation(" in runtime_support
        and "++result.discarded" in runtime_support,
        "ControlTask must discard translated Blynk commands from stale connection generations",
    )
    failures.require(
        "constexpr std::array<std::string_view, 10U> control_datastreams" in commands
        and "blynk_inbound_capacity = 16U" in command_header
        and "blynk_inbound_capacity - 1U" in commands
        and 'datastream == "CmdStop" && payload == "1"' in commands
        and "parse_decimal_milli" in commands
        and "void BlynkCommandMapper::disconnected()" in commands
        and "mapper_.disconnected()" in service
        and "std::from_chars" not in source_section(
            commands, "parse_decimal_milli(", "parse_temperature("
        ),
        "Blynk ingress must use the exact allowlist, fixed decimal parser, and reserved Stop slot",
    )
    failures.require(
        "app::SpscCommandMailbox http_mailbox" in runtime
        and "app::SpscCommandMailbox blynk_mailbox" in runtime
        and "external_commands.drain(" in runtime
        and "external_budget_per_cycle" in runtime_header
        and "regular_admission_capacity - 2U" in runtime_header
        and "blynk_first_ = !blynk_first_" in runtime_support
        and "if (is_stop)" in runtime_support,
        "ControlTask must fairly drain distinct HTTP/Blynk mailboxes with OTA headroom and Stop barrier",
    )
    failures.require(
        "std::atomic<std::uint32_t> session_sequence_" in runtime_header
        and "std::atomic<std::uint32_t> correlation_sequence_" in runtime_header
        and "internal_ota_correlation_id = 0xFFFFFFFEU" in runtime_header
        and "reserved_id(value)" in runtime_support,
        "HTTP/Blynk IDs must share atomic nonzero wrap-safe generators and skip internal OTA",
    )

    failures.require(
        "DRAM_ATTR StaticTask_t blynk_task_storage" in service
        and "blynk_task_stack_size_bytes = 12U * 1024U" in service
        and "blynk_task_priority = tskIDLE_PRIORITY + 1U" in service
        and re.search(
            r'xTaskCreateStaticPinnedToCore\(.*?"BlynkTask".*?\n\s*0\s*\n\s*\);',
            service,
            re.DOTALL,
        ) is not None
        and "esp_task_wdt_add" not in service
        and "snapshot_period_ms = 1000" in service,
        "BlynkTask must be static 12 KiB, low priority, core 0, 1 Hz, and outside TWDT",
    )
    for required in (
        "MQTT_TRANSPORT_OVER_SSL",
        "esp_crt_bundle_attach",
        "broker_port = 8883U",
        'credentials.username = "device"',
        "MQTT_PROTOCOL_V_3_1_1",
        "keepalive_seconds = 45U",
        "disable_clean_session = false",
        "reconnect_timeout_ms = reconnect_timeout_ms",
        'downlink_subscription[] = "downlink/ds/#"',
        "payload.data(),",
        "static_cast<int>(payload.size()), 0, 0",
    ):
        failures.require(required in service, f"M15 MQTT contract is missing: {required}")
    for forbidden in ('"get/ds', '"downlink/ota', "MQTT_PROTOCOL_V_5", "BlynkAir"):
        failures.require(forbidden not in service, f"forbidden Blynk MQTT behavior is present: {forbidden}")
    failures.require(
        "CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED=y" in defaults
        and "CONFIG_MQTT_USE_CORE_0=y" in defaults
        and "CONFIG_MQTT_SKIP_PUBLISH_IF_DISCONNECTED=y" in defaults
        and "# CONFIG_MQTT_TRANSPORT_WEBSOCKET is not set" in defaults,
        "sdkconfig.defaults must keep ESP-MQTT on core 0 and disable queued-disconnected/WebSocket behavior",
    )

    failures.require(
        "timer_configured" in remote
        and 'result_topic[] = "ds/LastCommandResult"' in service
        and "LastCommandResult" not in source_section(
            remote, "serialize_blynk_batch(", "BlynkRemoteProjection::observe"
        )
        and "blynk_payload_capacity = 960U" in remote_header
        and "blynk_status_minimum_interval_ms = 5000" in remote_header
        and "publish_succeeded" in remote_header
        and "results_.disconnected()" in service
        and "events_.disconnected()" in service,
        "status, result, event, timer-presence, payload, retry, and disconnect contracts are incomplete",
    )
    failures.require(
        'nvs_namespace[] = "fumuri_blynk"' in service
        and "FUMURI-BLYNK/1" in service
        and "blob_version = 1U" in provisioning
        and 'suffix = ".blynk.cloud"' in provisioning
        and "blynk_frame_crc32" in provisioning
        and "getpass.getpass" in tool
        and "--token" not in tool
        and 'subparsers.add_parser("status")' in tool
        and 'subparsers.add_parser("clear")' in tool
        and "configuration_.token.data()" not in source_section(
            service, "    void handle_provisioning(", "    void write_uart_response("
        ),
        "M15 provisioning must be bounded, redacted, versioned, regional, and CRC-protected",
    )
    repository_sources = "\n".join(
        path.read_text(errors="ignore")
        for root in ("components", "main", "tools")
        for path in (ROOT / root).rglob("*")
        if path.is_file() and (path.suffix in SOURCE_SUFFIXES or path.suffix == ".py")
        and path != Path(__file__).resolve()
    )
    failures.require(
        "BLYNK_AUTH_TOKEN" not in repository_sources
        and "local-secrets/blynk" not in repository_sources,
        "a build-time Blynk credential hook or token macro is forbidden",
    )
    for evidence in (
        "test_projection_connect_throttle_coalescing_and_retry",
        "test_status_timer_normalization_and_serializer_budget",
        "test_allowlisted_deterministic_command_mapping",
        "test_raw_mailbox_stop_reservation_and_concurrency",
        "test_disconnect_reconnect_boundary_discards_old_connection_state",
        "test_control_is_independent_of_blynk_transport",
        "test_translated_commands_do_not_cross_reconnect_boundary",
        "test_shared_ids_wrap_concurrency_and_fair_drain",
        "test_results_and_events_are_separate_and_not_replayed",
        "test_provisioning_blob_and_fragmented_parser",
    ):
        failures.require(evidence in tests, f"M15 host evidence is missing: {evidence}")


def main() -> int:
    failures = CheckFailures()
    check_layer_includes(failures)
    check_control_ownership(failures)
    check_component_graph(failures)
    check_v0_scope(failures)
    check_reproducible_build_contract(failures)
    check_m12_transport_contract(failures)
    check_m7_max31865_contract(failures)
    check_m9_ads1115_contract(failures)
    check_m14_history_contract(failures)
    check_m15_blynk_contract(failures)
    return failures.finish()


if __name__ == "__main__":
    raise SystemExit(main())

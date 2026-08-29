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
    expected_runtime = "components/smoker_platform/src/ordinary_runtime.cpp"
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
    runtime = (ROOT / "components/smoker_platform/src/ordinary_runtime.cpp").read_text()
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
        and 'esp-idf-lib/i2cdev: "==2.1.2"' in manifest
        and "esp-idf-lib__ads111x" in platform_cmake
        and "esp-idf-lib__i2cdev" in platform_cmake
        and "esp-idf-lib/ads111x:" in lock
        and "component_hash: fd18497adfb7210d750188986bc7cebc048db36abb64fdbe7216d4536083c4a2"
        in lock
        and "esp-idf-lib/i2cdev:" in lock
        and "component_hash: ad8981cc64533dcaced5107d72e42bcebe79345e194e82795792af531b300ce3"
        in lock
        and "esp-idf-lib/esp_idf_lib_helpers:" in lock
        and "component_hash: 689853bb8993434f9556af0f2816e808bf77b5d22100144b21f3519993daf237"
        in lock
        and re.search(
            r"direct_dependencies:\s*(?:\n- [^\n]+)*\n- esp-idf-lib/i2cdev(?:\n|$)",
            lock,
        ) is not None,
        "M9 must retain exact ADS1115/i2cdev pins and direct locked I2C ownership dependency",
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
        decision_ids == list(range(1, 60)),
        f"decision IDs must remain ordered and contiguous through D059; found {decision_ids}",
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
        and "974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979"
            in decisions
        and "39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d"
            in decisions
        and "smoker_platform" in decisions
        and "Production explicitly composes a deterministic adapter" in decisions
        and "synchronous safety evaluation and gate occur after requested-demand"
            in decisions
        and "automatic tuning is a separate future decision" in decisions,
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
        and "D059 records" in decisions
        and "synchronous safety latches `ChamberSensorInvalid`" in decisions,
        "D056 must preserve the exact-pinned MAX31865 dependency, evidence chronology, and fail-OFF boundary",
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
        and "Software accepts one or two explicitly" in decisions
        and "I2cdevSubsystemOwner" in decisions
        and "injected calibration/validity policy" in decisions
        and "CONFIG_I2CDEV_TIMEOUT" in decisions
        and "no ADS1115 value or failure directly changes" in decisions,
        "D057 must preserve the exact-pinned staged ADS1115/i2cdev ownership, inactive composition, physical gate, and monitoring-only boundary",
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
    failures.require(
        "## D059 — M7 activates MAX31865 as the ordinary authoritative chamber source"
        in decisions
        and "pull-independent SPI configuration reads" in decisions
        and "exact active `0xD1`" in decisions
        and "terminal `0x11`" in decisions
        and "provisional" in decisions
        and "same real ESP monotonic clock" in decisions
        and "descriptor, then releases the SPI bus" in decisions
        and "Food probes remain simulated" in decisions
        and "heater output remains simulated" in decisions
        and "latched session" in decisions
        and "controlled open/short" in decisions,
        "D059 must preserve the evidence-bounded MAX31865 production activation and mixed-I/O safety boundaries",
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
        "Could not allocate runtime context" in runtime
        and "Could not create ControlTask" in runtime
        and runtime.find("rollback_pending_firmware_and_reboot_if_needed();")
            > runtime.find("Could not allocate runtime context")
        and runtime.rfind("rollback_pending_firmware_and_reboot_if_needed();")
            > runtime.find("Could not create ControlTask"),
        "pending firmware must roll back when critical runtime construction fails",
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
    production_configuration = (
        platform
        / "include/smoker/platform/max31865_production_configuration.hpp"
    ).read_text()
    spi_bus_header = (
        platform / "include/smoker/platform/max31865_spi_bus.hpp"
    ).read_text()
    spi_bus = (platform / "src/max31865_spi_bus.cpp").read_text()
    monotonic_clock = (platform / "src/esp_monotonic_clock.cpp").read_text()
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
    runtime = (platform / "src/ordinary_runtime.cpp").read_text()
    runtime_header = (
        platform / "include/smoker/platform/ordinary_runtime.hpp"
    ).read_text()
    main_source = (ROOT / "main/app_main.cpp").read_text()
    tests = (ROOT / "tests/host/smoker_m7_tests.cpp").read_text()
    web_assets = (platform / "src/web_assets.hpp").read_text()
    readme = (ROOT / "README.md").read_text()
    architecture = (ROOT / "docs/ARCHITECTURE.md").read_text()
    roadmap = (ROOT / "docs/ROADMAP.md").read_text()
    traceability = (ROOT / "docs/TRACEABILITY.md").read_text()
    ota = (platform / "src/firmware_update_service.cpp").read_text()
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
        and '"src/max31865_spi_bus.cpp"' in platform_cmake
        and '"src/esp_monotonic_clock.cpp"' in platform_cmake
        and '"src/ordinary_runtime.cpp"' in platform_cmake
        and '"src/simulation_runtime.cpp"' not in platform_cmake
        and platform_cmake.find('"src/max31865_target_backend.cpp"')
            > platform_cmake.find("if(ESP_PLATFORM)"),
        "the MAX31865 policy must be host-buildable and its real runtime support target-only",
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
        and "result.celsius < validity_policy_.minimum_celsius" in sensor_read
        and "result.celsius > validity_policy_.maximum_celsius" in sensor_read
        and "last" not in sensor_read.lower(),
        "the MAX31865 chamber adapter must map only each current finite in-range valid sample",
    )
    failures.require(
        "reference_resistance_ohms;" in sensor_header
        and "Max31865FilterFrequency filter;" in sensor_header
        and "Max31865RtdStandard standard;" in sensor_header
        and "float minimum_celsius;" in sensor_header
        and "float maximum_celsius;" in sensor_header
        and "valid_max31865_temperature_validity_policy" in sensor
        and "policy.minimum_celsius < policy.maximum_celsius" in sensor
        and "std::isfinite(policy.minimum_celsius)" in sensor
        and "std::isfinite(policy.maximum_celsius)" in sensor
        and "spi_host_device_t spi_host;" in target_header
        and "gpio_num_t chip_select_gpio;" in target_header
        and "std::uint32_t clock_speed_hz;" in target_header,
        "unknown MAX31865 hardware and sensor-validity values must remain explicit required configuration",
    )

    for api in (
        "max31865_init_desc(",
        "max31865_get_fault_status(",
        "max31865_read_temperature(",
        "max31865_free_desc(",
    ):
        failures.require(api in target, f"the target MAX31865 backend must call {api}")
    failures.require(
        "MAX31865_3WIRE" in target
        and "max31865_pt100_nominal_ohms" in target
        and "~Max31865TargetBackend()" in target
        and "shutdown()" in target
        and "release_descriptor()" in target,
        "the active target backend must use explicit PT100/3-wire configuration and checked RAII shutdown",
    )
    failures.require(
        "max31865_set_config(" not in target
        and "max31865_clear_fault_status(" not in target
        and "write_exact_configuration(" in target
        and "read_exact_configuration(" in target
        and "write_and_verify_exact_configuration(" in target
        and "spi_device_transmit(" in target,
        "production must avoid driver read-modify-write configuration helpers and use exact checked register access",
    )

    production_text = "\n".join(
        path.read_text() for path in source_files("components", "main")
    )
    read_section = source_section(
        target,
        "Max31865ReadResult Max31865TargetBackend::read_continuous()",
        "bool Max31865TargetBackend::shutdown()",
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
    exact_config = configure_section.find("write_and_verify_exact_configuration(")
    readiness_reset = configure_section.find(
        "readiness_.continuous_configuration_applied()"
    )
    failures.require(
        bool(configure_section)
        and "readiness_.invalidate()" in configure_section
        and exact_config >= 0
        and readiness_reset > exact_config,
        "every exact verified continuous MAX31865 configuration must reset sample readiness",
    )
    initialize_section = source_section(
        target,
        "Max31865InitializationStatus Max31865TargetBackend::initialize()",
        "Max31865ReadResult Max31865TargetBackend::read_continuous()",
    )
    failures.require(
        "release_descriptor()" in initialize_section
        and "if (configured_)" not in initialize_section,
        "MAX31865 reinitialization must conservatively replace configuration and readiness",
    )
    recovery_section = source_section(
        target,
        "void Max31865TargetBackend::clear_fault_for_later_read()",
        "bool Max31865TargetBackend::valid_configuration()",
    )
    recovery_invalidate = recovery_section.find("readiness_.invalidate()")
    fault_clear = recovery_section.find("write_exact_configuration(clear_command)")
    recovery_configure = recovery_section.find("configure_device()")
    failures.require(
        recovery_invalidate >= 0
        and fault_clear > recovery_invalidate
        and recovery_configure > fault_clear,
        "MAX31865 fault recovery must invalidate readiness before exact clear/reconfiguration",
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
            forbidden not in read_section,
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
        "Max31865TargetBackend chamber_backend" in runtime
        and "Max31865ChamberSensor chamber" in runtime
        and "Max31865SpiBusOwner" in runtime
        and "EspMonotonicClock clock" in runtime
        and "SimulatedChamberSensor" not in runtime
        and "SimulatedFoodProbeSource food_source" in runtime
        and "DeterministicChamberController chamber_controller" in runtime
        and "SimulatedHeaterOutput heater" in runtime
        and "start_ordinary_runtime" in main_source
        and "start_ordinary_runtime" in runtime
        and '"smoker/platform/ordinary_runtime.hpp"' in main_source
        and "OrdinaryRuntimeConfiguration" in runtime_header
        and not (platform / "src/simulation_runtime.cpp").exists()
        and not (
            platform / "include/smoker/platform/simulation_runtime.hpp"
        ).exists(),
        "ordinary production composition must use MAX31865 chamber input with simulated food/heater and deterministic control",
    )
    failures.require(
        "spi_bus_initialize(" in spi_bus
        and "gpio_set_pull_mode(" in spi_bus
        and "GPIO_PULLUP_ONLY" in production_configuration
        and "GPIO_FLOATING" in spi_bus
        and "SPI_DMA_DISABLED" in spi_bus
        and "spi_bus_free(" in spi_bus
        and "owns_initialized_bus(" in spi_bus
        and "bus_owner_->owns_initialized_bus(configuration_.spi_host)" in target
        and "~Max31865SpiBusOwner()" in spi_bus
        and "Max31865SpiBusOwner(const Max31865SpiBusOwner&) = delete" in spi_bus_header,
        "the production SPI bus must have checked MISO pull-up and explicit non-copyable RAII ownership",
    )
    bus_initialize = spi_bus.find("spi_bus_initialize(")
    pull_setup = spi_bus.find("gpio_set_pull_mode(", bus_initialize)
    bus_release = spi_bus.find("spi_bus_free(")
    pull_cleanup = spi_bus.find("GPIO_FLOATING", bus_release)
    failures.require(
        bus_initialize >= 0
        and pull_setup > bus_initialize
        and bus_release > pull_setup
        and pull_cleanup > bus_release,
        "GPIO13 pull-up must follow bus initialization and floating cleanup must follow successful bus release",
    )
    failures.require(
        "esp_timer_get_time()" in monotonic_clock
        and "EspMonotonicClock clock" in runtime
        and "max31865_production_configuration.sensor" in runtime
        and "sensor_bus.get()" in runtime
        and "heater,\n              clock," in runtime,
        "MAX31865 readiness and application timing must share the real ESP monotonic clock",
    )

    for exact_value in (
        "SPI2_HOST",
        "GPIO_NUM_12",
        "GPIO_NUM_11",
        "GPIO_NUM_13",
        "GPIO_NUM_10",
        "100'000U",
        "100.0F",
        "430.0F",
        "-50.0F",
        "200.0F",
        "GPIO_PULLUP_ONLY",
        "MAX31865_3WIRE",
        "Max31865FilterFrequency::Hz50",
        "Max31865RtdStandard::Its90",
        "0xD1U",
        "0x11U",
        "std::chrono::milliseconds{66}",
    ):
        failures.require(
            exact_value in production_configuration,
            f"the centralized production MAX31865 configuration is missing {exact_value}",
        )
    failures.require(
        "T-pass connected evidence" in production_configuration
        and "Provisional operational choice" in production_configuration
        and "not a measurement" in production_configuration
        and "max31865_production_configuration.sensor" in runtime
        and "max31865_production_configuration.bus" in runtime,
        "production must centralize values and distinguish observed evidence from provisional choices",
    )

    bus_member = runtime.find("std::unique_ptr<Max31865SpiBusOwner> sensor_bus;")
    backend_member = runtime.find("Max31865TargetBackend chamber_backend;")
    chamber_member = runtime.find("Max31865ChamberSensor chamber;")
    startup_bus = runtime.find("sensor_bus->initialize()")
    startup_context = runtime.find("new (std::nothrow) RuntimeContext")
    startup_wait = runtime.find("wait_for_first_max31865_sample_boundary(context->clock)")
    startup_task_handoff = runtime.find("auto* const task_context = context.release()")
    task_create = runtime.find("xTaskCreateStaticPinnedToCore(")
    failures.require(
        bus_member >= 0
        and backend_member > bus_member
        and chamber_member > backend_member
        and startup_bus >= 0
        and startup_context > startup_bus
        and startup_wait > startup_context
        and startup_task_handoff > startup_wait
        and task_create > startup_task_handoff
        and "chamber_backend.shutdown()" in runtime,
        "the SPI bus must precede descriptor creation, bootstrap readiness must precede ControlTask, and descriptor shutdown must precede bus destruction",
    )
    startup = source_section(
        runtime,
        "bool start_ordinary_runtime(",
        "} // namespace smoker::platform",
    )
    sensor_bus_failure = startup.find(
        "SPI bus or checked GPIO13 MISO pull-up startup failed"
    )
    descriptor_failure = startup.find(
        "MAX31865 descriptor/configuration startup failed"
    )
    boundary_failure = startup.find(
        "MAX31865 first-sample bootstrap boundary failed"
    )
    runtime_allocation = startup.find("Could not allocate runtime context")
    task_failure = startup.find("Could not create ControlTask")
    failures.require(
        sensor_bus_failure >= 0
        and descriptor_failure > sensor_bus_failure
        and boundary_failure > descriptor_failure
        and "continuing with unavailable chamber input" in startup
        and "context->sensor_ready_for_bootstrap()" in startup
        and "context->chamber_backend.shutdown()" in startup
        and runtime_allocation >= 0
        and startup.find(
            "rollback_pending_firmware_and_reboot_if_needed();",
            runtime_allocation,
        ) > runtime_allocation
        and task_failure >= 0
        and startup.find(
            "rollback_pending_firmware_and_reboot_if_needed();",
            task_failure,
        ) > task_failure,
        "sensor hardware/bootstrap failures must continue into the observable runtime while critical allocation/task failures retain rollback",
    )
    failures.require(
        "snapshot.active_fault" in ota
        and "validation_fault_.store(true" in ota
        and "required_validation_cycles" in ota
        and "esp_ota_mark_app_valid_cancel_rollback()" in ota,
        "a sensor-faulting pending image must use normal fault rollback and retain the five-safe-cycle mark-valid contract",
    )
    failures.require(
        "I/O MIXT" in web_assets
        and "I/O SIMULAT" not in web_assets
        and "ordinary mixed-I/O" in readme
        and "ordinary mixed-I/O" in architecture
        and "ordinary runtime" in roadmap
        and "ordinary-runtime contracts" in traceability
        and "SimulationContext" not in traceability,
        "active UI/docs must describe the ordinary mixed-I/O composition and current runtime name",
    )
    control_loop = source_section(runtime, "void control_task(", "bool start_ordinary_runtime(")
    failures.require(
        bool(control_loop)
        and "application.tick();" in control_loop
        and "clock.advance(" not in control_loop
        and "Max31865" not in control_loop.split("application.tick();", 1)[0]
        and "xTaskCreate" not in control_loop,
        "the sole ControlTask must perform ordinary MAX31865 reads only through the application tick using real time",
    )
    failures.require(
        "gpio_set_level(" not in runtime
        and "gpio_set_level(" not in target
        and "gpio_set_level(" not in spi_bus
        and "Ssr" not in runtime
        and "GpioHeater" not in runtime,
        "ordinary production must not introduce SSR/GPIO heater output",
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
        "start_ordinary_runtime",
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
        "test_max31865_later_driver_failure_latches_without_cached_temperature",
        "test_max31865_temperature_validity_policy_is_finite_ordered_and_inclusive",
        "test_max31865_temperature_validity_accepts_boundaries_and_rejects_outside",
        "test_max31865_initialization_failure_while_idle_latches_safety_fault",
        "test_max31865_valid_then_out_of_range_latches_without_cached_temperature",
    ):
        failures.require(evidence in tests, f"M7 host evidence is missing: {evidence}")


def check_m8_pid_contract(failures: CheckFailures) -> None:
    platform = ROOT / "components/smoker_platform"
    port_header = (
        ROOT / "components/smoker_app/include/smoker/app/ports.hpp"
    ).read_text()
    application_header = (
        ROOT / "components/smoker_app/include/smoker/app/smoker_application.hpp"
    ).read_text()
    application = (
        ROOT / "components/smoker_app/src/smoker_application.cpp"
    ).read_text()
    controller_header = (
        platform / "include/smoker/platform/pid_chamber_controller.hpp"
    ).read_text()
    controller = (platform / "src/pid_chamber_controller.cpp").read_text()
    target_header = (
        platform / "include/smoker/platform/pid_target_backend.hpp"
    ).read_text()
    target = (platform / "src/pid_target_backend.cpp").read_text()
    simulated_header = (
        platform / "include/smoker/platform/simulated_adapters.hpp"
    ).read_text()
    simulated = (platform / "src/simulated_adapters.cpp").read_text()
    runtime = (platform / "src/ordinary_runtime.cpp").read_text()
    main_source = (ROOT / "main/app_main.cpp").read_text()
    platform_cmake = (platform / "CMakeLists.txt").read_text()
    manifest = (platform / "idf_component.yml").read_text()
    lock = (ROOT / "dependencies.lock").read_text()
    pid_lock = source_section(
        lock, "  espressif/pid_ctrl:\n", "  idf:\n"
    )
    iqmath_lock = source_section(
        lock, "  espressif/iqmath:\n", "  espressif/mdns:\n"
    )
    tests_cmake = (ROOT / "tests/CMakeLists.txt").read_text()
    tests = (ROOT / "tests/host/smoker_m8_tests.cpp").read_text()

    lower_layer_text = "\n".join(
        path.read_text()
        for path in source_files("components/smoker_core", "components/smoker_app")
    )
    failures.require(
        "pid_ctrl.h" not in lower_layer_text
        and "pid_ctrl_" not in lower_layer_text
        and "esp_err" not in lower_layer_text,
        "pid_ctrl and ESP-IDF types must remain outside smoker_core/smoker_app",
    )
    failures.require(
        "class IChamberController" in port_header
        and "std::optional<core::HeaterDemand> request(" in port_header
        and "core::Temperature chamber_temperature" in port_header
        and "core::Temperature chamber_target" in port_header
        and "bool reset() noexcept" in port_header
        and "critical lifecycle path" in port_header
        and port_header.count("wait or block") >= 2
        and port_header.count("create tasks") >= 2
        and port_header.count("allocate") >= 2
        and port_header.count("steady state") >= 2
        and "IChamberController& chamber_controller" in application_header,
        "the application controller port must be synchronous, typed, resettable, ESP-IDF-free, and prohibit blocking/side effects/allocation in request and reset",
    )
    failures.require(
        'espressif/pid_ctrl: "==0.3.1"' in manifest
        and "espressif__pid_ctrl" in platform_cmake
        and "component_hash: 974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979"
        in pid_lock
        and "version: 0.3.1" in pid_lock
        and "name: espressif/iqmath" in pid_lock
        and "component_hash: 39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d"
        in iqmath_lock
        and "version: 1.11.0~1" in iqmath_lock,
        "M8 must retain the exact pid_ctrl 0.3.1 pin and actual locked component/transitive hashes",
    )
    failures.require(
        '"src/pid_chamber_controller.cpp"' in platform_cmake
        and '"src/pid_target_backend.cpp"' in platform_cmake
        and platform_cmake.find('"src/pid_target_backend.cpp"')
            > platform_cmake.find("if(ESP_PLATFORM)"),
        "the PID policy must be host-buildable and the real pid_ctrl backend target-only",
    )
    failures.require(
        "class PidChamberController final : public app::IChamberController"
            in controller_header
        and "class IPidControllerBackend" in controller_header
        and "struct PositionalAccumulatedErrorBounds" in controller_header
        and "positional_accumulated_error_bounds" in controller_header
        and "PidCalculationForm::Positional" in controller
        and "PidCalculationForm::Incremental" in controller
        and "return !configuration.positional_accumulated_error_bounds" in controller
        and "std::isfinite" in controller
        and "chamber_target.celsius() - chamber_temperature.celsius()"
            in controller
        and "core::HeaterDemand::from_percent(output_percent)" in controller,
        "the host PID boundary must validate explicit configuration, sign, and normalized output",
    )
    failures.require(
        "value_or(" in target
        and "PositionalAccumulatedErrorBounds{0.0F, 0.0F}" in target
        and ".max_integral = positional_bounds.maximum" in target
        and ".min_integral = positional_bounds.minimum" in target,
        "the target PID backend must map ignored incremental integral fields deterministically to 0/0",
    )
    for api in (
        "pid_new_control_block_f(",
        "pid_compute_f(",
        "pid_reset_ctrl_block_f(",
        "pid_del_control_block_f(",
    ):
        failures.require(api in target, f"the target PID backend must call exact API {api}")
    failures.require(
        "pid_ctrl_block_handle_f_t" in target_header
        and "~EspressifPidFloatBackend()" in target
        and "release();" in target
        and "EspressifPidFloatBackend(const EspressifPidFloatBackend&) = delete"
            in target_header,
        "the target float PID backend must retain non-copyable RAII lifecycle ownership",
    )
    failures.require(
        "~PidChamberController" not in controller_header
        and "PidChamberController::~PidChamberController" not in controller,
        "application-owned reported resets plus target-backend RAII release must own PID lifecycle; the common wrapper must not hide a destructor reset failure",
    )

    constructor = source_section(
        application,
        "SmokerApplication::SmokerApplication(",
        "bool SmokerApplication::submit(",
    )
    constructor_off_position = constructor.find(
        "heater_output_.write(core::HeaterDemand::off())"
    )
    constructor_reset_position = constructor.find("reset_chamber_controller()")
    failures.require(
        constructor_off_position >= 0
        and constructor_reset_position > constructor_off_position,
        "SmokerApplication construction must issue observable heater OFF before the first chamber-controller reset callback",
    )

    tick = source_section(application, "void SmokerApplication::tick()", "SmokerSnapshot SmokerApplication::snapshot()")
    request_position = tick.find("chamber_controller_.request(")
    safety_position = tick.find("evaluate_safety(now)")
    gate_position = tick.find("core::apply_safety_gate(")
    write_position = tick.find("heater_output_.write(heater_demand_)")
    failures.require(
        request_position >= 0
        and safety_position > request_position
        and gate_position > safety_position
        and write_position > gate_position
        and "ControlLoopFailure" in tick
        and "reset_chamber_controller()" in tick,
        "controller request must precede authoritative safety/gate/write with fail-closed reset handling",
    )
    failures.require(
        "class DeterministicChamberController final : public app::IChamberController"
            in simulated_header
        and "core::calculate_heater_demand" in simulated
        and "DeterministicChamberController chamber_controller" in runtime
        and "chamber_controller," in runtime
        and "PidChamberController" not in runtime
        and "EspressifPidFloatBackend" not in runtime
        and "PidChamberController" not in main_source
        and "EspressifPidFloatBackend" not in main_source
        and "SimulatedHeaterOutput heater" in runtime,
        "production must retain deterministic 100/0 simulated composition without PID/SSR activation",
    )

    project_pid_paths = [
        controller_header,
        controller,
        target_header,
        target,
    ]
    forbidden_patterns = (
        r"\bxTaskCreate",
        r"\bvTaskDelay\s*\(",
        r"\bxTaskDelay",
        r"\bsleep\s*\(",
        r"\busleep\s*\(",
        r"\bmalloc\s*\(",
        r"\bcalloc\s*\(",
        r"\brealloc\s*\(",
        r"\boperator\s+new",
        r"\bESP_LOG",
        r"\bprintf\s*\(",
        r"\bfprintf\s*\(",
        r"\bfopen\s*\(",
        r"\bsocket\s*\(",
        r"\bmutex",
    )
    for pattern in forbidden_patterns:
        failures.require(
            all(re.search(pattern, text, re.IGNORECASE) is None for text in project_pid_paths),
            f"project-owned PID paths must not contain side effect/allocation pattern {pattern}",
        )

    failures.require(
        'add_executable(smoker_m8_tests' in tests_cmake
        and "smoker_v0.m8_pid" in tests_cmake,
        "the focused M8 host group must be registered",
    )
    for evidence in (
        "test_valid_positional_configuration_with_accumulated_error_bounds",
        "test_invalid_common_and_positional_configurations",
        "test_valid_incremental_configuration_has_no_integral_bound_promise",
        "test_incremental_configuration_rejects_contradictory_positional_bounds",
        "test_backend_initialization_failure",
        "test_target_minus_measurement_and_normalized_output",
        "test_backend_compute_and_output_failures",
        "test_reset_failure_and_steady_allocation_without_destructor_reset",
        "test_constructor_writes_observable_off_before_first_controller_reset",
        "test_application_off_reset_and_stop_lifecycle",
        "test_same_tick_target_removal_and_restoration_uses_final_state",
        "test_invalid_measurement_fault_resets_and_never_resumes",
        "test_compute_failure_latches_and_requires_clear_then_start",
        "test_reset_failure_fails_closed_and_can_only_resolve_latched_fault",
        "test_safety_overrides_positive_request_before_only_write",
        "test_firmware_update_interlock_never_calls_controller",
        "test_deterministic_production_adapter_preserves_m2_behavior",
    ):
        failures.require(evidence in tests, f"M8 host evidence is missing: {evidence}")

    managed = ROOT / "managed_components/espressif__pid_ctrl"
    managed_iqmath = ROOT / "managed_components/espressif__iqmath"
    managed_inputs = (
        managed / ".component_hash",
        managed / "include/pid_ctrl.h",
        managed / "src/pid_ctrl_f.c",
        managed_iqmath / ".component_hash",
    )
    if managed.exists() or managed_iqmath.exists() or any(
        path.exists() for path in managed_inputs
    ):
        missing_inputs = [relative(path) for path in managed_inputs if not path.is_file()]
        failures.require(
            not missing_inputs,
            "managed PID guardrail inputs are partially present; missing "
            + ", ".join(missing_inputs),
        )
        if missing_inputs:
            return

        try:
            component_hash = managed_inputs[0].read_text().strip()
            upstream_header = managed_inputs[1].read_text()
            upstream_float = managed_inputs[2].read_text()
            iqmath_hash = managed_inputs[3].read_text().strip()
        except (OSError, UnicodeError) as error:
            failures.require(
                False,
                f"managed PID guardrail inputs could not be read as text: {error}",
            )
            return

        failures.require(
            component_hash
                == "974be0666bb4d95f49677327dd8305781d04d8bae284fdde2fbadf06ca9d4979",
            "present managed pid_ctrl source must match the locked component hash",
        )
        failures.require(
            iqmath_hash
                == "39448db759b410373e543798167ca4670bbff3019cb290a2fe8e627221e71b9d",
            "present managed iqmath dependency must match the locked component hash",
        )
        failures.require(
            all(api in upstream_header for api in (
                "pid_new_control_block_f(", "pid_compute_f(",
                "pid_reset_ctrl_block_f(", "pid_del_control_block_f(",
                "pid_new_control_block_iq(", "PID_CAL_TYPE_INCREMENTAL",
                "PID_CAL_TYPE_POSITIONAL",
            ))
            and not re.search(
                r"autotun|plant.ident|sample.period|delta.time|\bdt\b",
                upstream_header,
                re.IGNORECASE,
            ),
            "present managed pid_ctrl API must retain reviewed backends/forms and no dt/autotune surface",
        )
        positional = source_section(
            upstream_float,
            "static float pid_calc_positional_f(",
            "static float pid_calc_incremental_f(",
        )
        incremental = source_section(
            upstream_float,
            "static float pid_calc_incremental_f(",
            "esp_err_t pid_update_parameters_f(",
        )
        failures.require(
            "pid->integral_err += error" in positional
            and "pid->integral_err = pid_clamp_f(" in positional
            and "pid->min_integral" in positional
            and "pid->max_integral" in positional
            and "pid->integral_err * pid->ki" in positional
            and "(error - pid->previous_err1) * pid->kd" in positional,
            "present managed positional PID must accumulate/clamp raw per-call error, multiply it by Ki, and differentiate error",
        )
        failures.require(
            incremental
            and all(token not in incremental for token in (
                "integral_err", "min_integral", "max_integral",
            ))
            and "pid->last_output" in incremental
            and "pid_clamp_f(output, pid->min_output, pid->max_output)"
                in incremental
            and "error - pid->previous_err1 - pid->previous_err1 + pid->previous_err2"
                in incremental
            and "pid->last_output = output" in incremental,
            "present managed incremental PID must ignore integral-limit fields, differentiate error, and retain/clamp last output",
        )
        unsupported_surface = upstream_header + "\n" + upstream_float
        failures.require(
            "pid_compute_f(pid_ctrl_block_handle_f_t pid, float input_error, float *ret_result)"
                in upstream_header
            and not re.search(
                r"autotun|plant.ident|derivative.?filter|derivative.?on.?measurement|sample.?time|delta.?time|\bdt\b",
                unsupported_surface,
                re.IGNORECASE,
            ),
            "present managed PID must retain an error-only per-call API with no dt, derivative filter/on-measurement, autotuning, or plant-identification surface",
        )
        creation = source_section(
            upstream_float,
            "esp_err_t pid_new_control_block_f(",
            "esp_err_t pid_del_control_block_f(",
        )
        compute = source_section(
            upstream_float,
            "esp_err_t pid_compute_f(",
            "esp_err_t pid_reset_ctrl_block_f(",
        )
        reset = upstream_float.partition("esp_err_t pid_reset_ctrl_block_f(")[2]
        failures.require(
            "calloc(" in creation
            and all(token not in compute + reset for token in (
                "calloc(", "malloc(", "realloc(", "free(",
            )),
            "present managed float source must allocate at creation, not valid compute/reset",
        )


def check_m9_ads1115_contract(failures: CheckFailures) -> None:
    platform = ROOT / "components/smoker_platform"
    source_header = (
        platform / "include/smoker/platform/ads1115_food_probe_source.hpp"
    ).read_text()
    target_header = (
        platform / "include/smoker/platform/ads1115_target_backend.hpp"
    ).read_text()
    subsystem_header = (
        platform / "include/smoker/platform/i2cdev_subsystem.hpp"
    ).read_text()
    source = (platform / "src/ads1115_food_probe_source.cpp").read_text()
    target = (platform / "src/ads1115_target_backend.cpp").read_text()
    subsystem = (platform / "src/i2cdev_subsystem.cpp").read_text()
    platform_cmake = (platform / "CMakeLists.txt").read_text()
    runtime = (platform / "src/ordinary_runtime.cpp").read_text()
    main_source = (ROOT / "main/app_main.cpp").read_text()
    tests = (ROOT / "tests/host/smoker_m9_tests.cpp").read_text()
    pinned_driver_path = (
        ROOT / "managed_components/esp-idf-lib__ads111x/ads111x.c"
    )
    pinned_driver = (
        pinned_driver_path.read_text() if pinned_driver_path.is_file() else None
    )
    managed_i2cdev_dir = ROOT / "managed_components/esp-idf-lib__i2cdev"
    pinned_i2cdev_source_path = managed_i2cdev_dir / "i2cdev.c"
    pinned_i2cdev_header_path = managed_i2cdev_dir / "i2cdev.h"
    pinned_i2cdev_source = None
    pinned_i2cdev_header = None
    if managed_i2cdev_dir.exists():
        failures.require(
            pinned_i2cdev_source_path.is_file() and pinned_i2cdev_header_path.is_file(),
            "present managed i2cdev input must contain complete i2cdev.c/i2cdev.h lifecycle sources",
        )
        if pinned_i2cdev_source_path.is_file() and pinned_i2cdev_header_path.is_file():
            pinned_i2cdev_source = pinned_i2cdev_source_path.read_text()
            pinned_i2cdev_header = pinned_i2cdev_header_path.read_text()

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
        and '"src/i2cdev_subsystem.cpp"' in platform_cmake
        and platform_cmake.find('"src/ads1115_target_backend.cpp"')
            > platform_cmake.find("if(ESP_PLATFORM)"),
        "the ADS1115 sequencer must be host-buildable and target backend/subsystem owner target-only",
    )

    failures.require(
        "class Ads1115FoodProbeSource final : public app::IFoodProbeSource"
            in source_header
        and "class IAds1115Backend" in source_header
        and "class IAds1115SampleConverter" in source_header
        and "void service() noexcept;" in source_header
        and "std::vector<std::optional<CachedSample>> samples_" in source_header
        and "enum class DeviceState" in source_header
        and "Unsynchronized" in source_header
        and "std::array<DeviceState, 2U> device_states_" in source_header
        and "AcquisitionState state_" in source_header,
        "M9 must retain one host-testable acquisition owner with timestamped caches and per-device state",
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
        "devices.empty()" in source
        and "devices.size() > ads1115_maximum_device_count" in source
        and "devices.size() == ads1115_maximum_device_count" in source
        and "compatible_device_pair" in source
        and "first.address != second.address" in source
        and "first.i2c_port == second.i2c_port" in source
        and "device_has_channel" in source
        and "channels[earlier].probe_id == channel.probe_id" in source
        and "minimum_ads1115_conversion_timeout(channel.data_rate)" in source,
        "M9 configuration must validate one/two devices, shared buses, mappings, IDs, and deadlines",
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
        "void Ads1115FoodProbeSource::invalidate_active_probe()",
    )
    failures.require(
        bool(start_section)
        and "device_state == DeviceState::Unsynchronized" in start_section
        and start_section.find("backend_.conversion_busy(channel.device_index, busy)")
            < start_section.find("backend_.configure_channel(channel)")
        and "backend_.configure_channel(channel)" in start_section
        and "backend_.start_conversion(channel.device_index)" in start_section
        and start_section.find("backend_.start_conversion(channel.device_index)")
            < start_section.find("conversion_deadline_ = clock_.now()")
        and "device_state = DeviceState::Unsynchronized" in start_section
        and "backend_.get_value" not in start_section
        and bool(poll_section)
        and poll_section.find("backend_.conversion_busy(")
            < poll_section.find("clock_.now() < conversion_deadline_")
        and poll_section.find("clock_.now() < conversion_deadline_")
            < poll_section.find("backend_.get_value(")
        and poll_section.find("backend_.get_value(")
            < poll_section.find("sample_converter_.convert("),
        "M9 must synchronize before start, calculate the deadline after start, and observe busy before deadline/read/calibration",
    )
    failures.require(
        "device_states_.fill(DeviceState::Unsynchronized);" in source
        and "device_state = DeviceState::Idle;\n        return;" in start_section
        and "void Ads1115FoodProbeSource::quarantine_active_device()" in source
        and "samples_[active_channel_].reset();" in source
        and "device_states_[device_index] = DeviceState::Unsynchronized;" in source
        and poll_section.count("quarantine_active_device();") >= 2
        and "device_state = DeviceState::Idle;" in poll_section
        and "invalidate_active_probe();\n        advance_after_active_conversion();" in poll_section
        and "next_channel_ = (active_channel_ + 1U)" in source,
        "M9 must begin unsynchronized, discard recovery in its own step, quarantine ambiguous devices, and clear only the affected cache",
    )
    if pinned_driver is not None:
        failures.require(
            "if (offs != OS_OFFSET || mask != OS_MASK)" in pinned_driver
            and "old &= ~(OS_MASK << OS_OFFSET);" in pinned_driver
            and "esp_err_t ads111x_set_input_mux" in pinned_driver
            and "esp_err_t ads111x_set_gain" in pinned_driver
            and "esp_err_t ads111x_set_data_rate" in pinned_driver,
            "the pinned 1.1.14 configuration-failure classification must remain grounded in its OS-clearing read-modify-write source",
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
        and "std::size_t descriptor_count_{0U}" in target_header
        and "explicit Ads1115TargetBackend(I2cdevSubsystemOwner& subsystem)"
            in target_header
        and "~Ads1115TargetBackend()" in target
        and "bool Ads1115TargetBackend::shutdown()" in target,
        "the target backend must own one/two active descriptors and expose checked RAII cleanup",
    )

    backend_shutdown = source_section(
        target,
        "bool Ads1115TargetBackend::shutdown()",
        "} // namespace smoker::platform",
    )
    failures.require(
        "!subsystem_.active()" in initialize_section
        and initialize_section.find("subsystem_.acquire_descriptor_owner()")
            < initialize_section.find("ads111x_init_desc(")
        and "devices.empty()" in initialize_section
        and "devices.size() > descriptors_.size()" in initialize_section
        and "device_index < descriptor_count_" in target
        and bool(backend_shutdown)
        and "bool all_released = true;" in backend_shutdown
        and "ads111x_free_desc(" in backend_shutdown
        and backend_shutdown.find("ads111x_free_desc(")
            < backend_shutdown.find("subsystem_.release_descriptor_owner()")
        and "all_released = false;" in backend_shutdown,
        "the target backend must require active subsystem ownership, bound every call to the configured count, and aggregate descriptor cleanup before releasing its lease",
    )

    owner_initialize = source_section(
        subsystem,
        "bool I2cdevSubsystemOwner::initialize()",
        "bool I2cdevSubsystemOwner::shutdown()",
    )
    owner_shutdown = source_section(
        subsystem,
        "bool I2cdevSubsystemOwner::shutdown()",
        "bool I2cdevSubsystemOwner::active()",
    )
    failures.require(
        "class I2cdevSubsystemOwner final" in subsystem_header
        and "I2cdevSubsystemOwner(const I2cdevSubsystemOwner&) = delete;"
            in subsystem_header
        and "I2cdevSubsystemOwner(I2cdevSubsystemOwner&&) = delete;"
            in subsystem_header
        and "Lifecycle::NeverInitialized" in owner_initialize
        and owner_initialize.find("i2cdev_init()")
            < owner_initialize.find("Lifecycle::Active")
        and "lifecycle_ = Lifecycle::Released;" in owner_initialize
        and bool(owner_shutdown)
        and owner_shutdown.find("descriptor_owner_active_")
            < owner_shutdown.find("i2cdev_done()")
        and owner_shutdown.find("i2cdev_done()")
            < owner_shutdown.find("lifecycle_ = Lifecycle::Released")
        and "return result == ESP_OK;" in owner_shutdown
        and "static " not in subsystem
        and "~I2cdevSubsystemOwner()" in subsystem
        and "i2cdev_done()" in subsystem,
        "the target subsystem owner must be non-copyable, project-global-free, initialize once, reject restart, and report checked shutdown after descriptor release",
    )

    lifecycle_calls = find_calls(
        r"\bi2cdev_(?:init|done)\s*\(",
        source_files("components", "main"),
    )
    lifecycle_call_owners = {relative(path) for path, _ in lifecycle_calls}
    failures.require(
        lifecycle_call_owners
            == {"components/smoker_platform/src/i2cdev_subsystem.cpp"}
        and "i2cdev_init(" in subsystem
        and "i2cdev_done(" in subsystem,
        "i2cdev_init()/i2cdev_done() calls are permitted only in the target subsystem owner",
    )

    if pinned_i2cdev_source is not None and pinned_i2cdev_header is not None:
        upstream_init = source_section(
            pinned_i2cdev_source,
            "esp_err_t i2cdev_init(void)",
            "esp_err_t i2c_dev_create_mutex(",
        )
        upstream_done = source_section(
            pinned_i2cdev_source,
            "esp_err_t i2cdev_done(void)",
            "esp_err_t i2cdev_get_shared_handle(",
        )
        upstream_delete_mutex = source_section(
            pinned_i2cdev_source,
            "esp_err_t i2c_dev_delete_mutex(",
            "esp_err_t i2c_dev_take_mutex(",
        )
        upstream_setup = source_section(
            pinned_i2cdev_source,
            "static esp_err_t i2c_setup_port(",
            "static esp_err_t i2c_setup_device(",
        )
        failures.require(
            "static bool initialized = false;" in upstream_init
            and "if (initialized)" in upstream_init
            and "initialized = true" in upstream_init
            and "initialized = false" not in upstream_done
            and "vSemaphoreDelete(i2c_ports[i].lock);" in upstream_done
            and "i2c_ports[i].lock = NULL;" in upstream_done
            and "if (!port_state->lock)" in upstream_setup
            and "call i2cdev_init() first" in upstream_setup
            and "before any I2C devices are initialized" in pinned_i2cdev_header,
            "present locked i2cdev 2.1.2 source must retain the init-before-I/O and non-restartable-after-done lifecycle assumed by the owner",
        )
        failures.require(
            "return ESP_ERR_TIMEOUT;" in upstream_delete_mutex
            and "i2c_master_bus_rm_device(" in upstream_delete_mutex
            and "if (rm_res != ESP_OK)" in upstream_delete_mutex
            and "dev->dev_handle = NULL;" in upstream_delete_mutex
            and "return rm_res" not in upstream_delete_mutex
            and "i2c_del_master_bus(" in upstream_delete_mutex
            and "if (del_bus_res != ESP_OK)" in upstream_delete_mutex
            and "port_state->bus_handle = NULL;" in upstream_delete_mutex
            and "return del_bus_res" not in upstream_delete_mutex
            and "return ESP_OK;" in upstream_delete_mutex,
            "present locked i2cdev 2.1.2 source must retain the cleanup-observability boundary: port-lock timeout is reported while nested device/bus deletion failures are swallowed before ESP_OK",
        )
        failures.require(
            pinned_driver is not None
            and "i2c_dev_create_mutex(dev)" in source_section(
                pinned_driver,
                "esp_err_t ads111x_init_desc(",
                "esp_err_t ads111x_free_desc(",
            ),
            "present locked ads111x source must retain descriptor-mutex creation in ads111x_init_desc()",
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
        "i2cdev_done(",
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
        and "start_ordinary_runtime" in main_source
        and "Ads1115FoodProbeSource" not in runtime
        and "Ads1115TargetBackend" not in runtime
        and "I2cdevSubsystemOwner" not in runtime
        and "Ads1115FoodProbeSource" not in main_source
        and "Ads1115TargetBackend" not in main_source
        and "I2cdevSubsystemOwner" not in main_source
        and "i2cdev_init(" not in runtime
        and "i2cdev_done(" not in runtime
        and "i2cdev_init(" not in main_source
        and "i2cdev_done(" not in main_source
        and "Max31865ChamberSensor" in runtime
        and "DeterministicChamberController" in runtime
        and "SimulatedHeaterOutput" in runtime,
        "production composition must retain real chamber plus simulated food/heater and must not own ADS1115/i2cdev",
    )

    for evidence in (
        "test_ads1115_invalid_incomplete_configurations_are_rejected",
        "test_ads1115_one_device_sequencer_never_touches_device_one",
        "test_ads1115_both_devices_require_initial_idle_synchronization",
        "test_ads1115_initial_stale_result_is_discarded_before_later_restart",
        "test_ads1115_fake_latches_in_flight_provenance_across_reconfiguration",
        "test_ads1115_ready_exactly_at_deadline_is_accepted",
        "test_ads1115_ready_after_deadline_is_accepted",
        "test_ads1115_busy_exactly_at_deadline_quarantines_without_read",
        "test_ads1115_timed_out_conversion_cannot_be_reconfigured_or_misattributed",
        "test_ads1115_ambiguously_failed_start_is_quarantined_and_discarded",
        "test_ads1115_busy_observation_error_uses_the_same_quarantine_boundary",
        "test_ads1115_quarantined_device_does_not_block_the_other_adc",
        "test_ads1115_recovery_requires_idle_and_never_reads_or_restarts_same_step",
        "test_ads1115_per_probe_failures_clear_only_the_affected_sample",
        "test_ads1115_idle_configure_get_and_calibration_failures_do_not_quarantine",
        "test_ads1115_cached_readings_expire_and_unknown_ids_are_absent",
        "test_ads1115_steady_state_service_and_read_are_observed_allocation_free",
        "test_ads1115_missing_or_invalid_food_probe_never_changes_chamber_control",
    ):
        failures.require(evidence in tests, f"M9 host evidence is missing: {evidence}")


def check_m14_history_contract(failures: CheckFailures) -> None:
    runtime = (ROOT / "components/smoker_platform/src/ordinary_runtime.cpp").read_text()
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
    control_loop = source_section(runtime, "void control_task(", "bool start_ordinary_runtime(")
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
        "HistoryWritePolicy write_policy" in history
        and "write_policy.has_pending_lifecycle()" in history
        and "HistoryWriteCycleResult::TerminalFailStop" in history
        and history.count("History storage FAILED; stopping flash persistence") == 1
        and "failed_write_threshold" not in history,
        "HistoryTask must use log-owned FAILED state for one-shot terminal fail-stop",
    )
    write_policy = source_section(
        header,
        "class HistoryWritePolicy final {",
        "} // namespace smoker::platform",
    )
    terminal_check = write_policy.find("if (terminal_)")
    writer_call = write_policy.find("std::forward<Writer>(writer)")
    failed_transition = write_policy.find(
        "attempt.storage_state == HistoryStorageState::Failed"
    )
    failures.require(
        terminal_check >= 0
        and writer_call > terminal_check
        and failed_transition > writer_call
        and "pending_lifecycle_.reset();\n            terminal_ = true;" in write_policy
        and "return HistoryWriteCycleResult::Stopped;" in write_policy,
        "the portable history write policy must retry lifecycle only before FAILED and never call its writer afterward",
    )
    failures.require(
        "&& !initialized_.load(std::memory_order_acquire)" in history
        and "auto result = log_.health();" in history
        and "result.state = HistoryStorageState::Failed;" in history,
        "runtime history failure health must retain initialized CircularHistoryLog counters",
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
        "test_start_transient_failure_is_retried",
        "test_end_transient_failure_is_retried",
        "test_lifecycle_terminal_failure_stops_writes",
        "test_ordinary_terminal_failure_stops_writes",
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
    runtime = (platform / "src/ordinary_runtime.cpp").read_text()
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
    check_m8_pid_contract(failures)
    check_m9_ads1115_contract(failures)
    check_m14_history_contract(failures)
    check_m15_blynk_contract(failures)
    return failures.finish()


if __name__ == "__main__":
    raise SystemExit(main())

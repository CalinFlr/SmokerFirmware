#!/usr/bin/env python3
"""Validate the generated ESP-IDF configuration required through M15."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


REQUIRED_VALUES = {
    "CONFIG_IDF_TARGET": '"esp32s3"',
    "CONFIG_BOOTLOADER_OFFSET_IN_FLASH": "0x0",
    "CONFIG_ESPTOOLPY_FLASHMODE": '"dio"',
    "CONFIG_ESPTOOLPY_FLASHFREQ": '"80m"',
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM": "y",
    "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME": '"partitions.csv"',
    "CONFIG_PARTITION_TABLE_OFFSET": "0x8000",
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE": "y",
    "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE": "y",
    "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL": "y",
    "CONFIG_SECURE_SIGNED_ON_UPDATE": "y",
    "CONFIG_SECURE_SIGNED_APPS": "y",
    "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT": "y",
    "CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME": "y",
    "CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT": "y",
    "CONFIG_MQTT_PROTOCOL_311": "y",
    "CONFIG_MQTT_TRANSPORT_SSL": "y",
    "CONFIG_MQTT_SKIP_PUBLISH_IF_DISCONNECTED": "y",
    "CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED": "y",
    "CONFIG_MQTT_USE_CORE_0": "y",
}

REQUIRED_UNSET = {
    "CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES",
    "CONFIG_MQTT_PROTOCOL_5",
    "CONFIG_MQTT_TRANSPORT_WEBSOCKET",
    "CONFIG_MQTT_USE_CORE_1",
}


def parse_sdkconfig(path: Path) -> tuple[dict[str, str], set[str]]:
    values: dict[str, str] = {}
    unset: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
            continue
        if line.startswith("# CONFIG_") and line.endswith(" is not set"):
            unset.add(line[2 : -len(" is not set")])
    return values, unset


def configuration_failures(path: Path) -> list[str]:
    if not path.is_file():
        return [f"generated sdkconfig is missing: {path}"]

    values, unset = parse_sdkconfig(path)
    failures: list[str] = []
    for key, expected in REQUIRED_VALUES.items():
        actual = values.get(key)
        if actual != expected:
            rendered = "unset" if key in unset or actual is None else actual
            failures.append(f"{key}: expected {expected}, found {rendered}")
    for key in REQUIRED_UNSET:
        if key not in unset:
            actual = values.get(key, "missing")
            failures.append(f"{key}: expected disabled, found {actual}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check the effective generated ESP-IDF M15 configuration."
    )
    parser.add_argument("sdkconfig", type=Path)
    arguments = parser.parse_args()

    failures = configuration_failures(arguments.sdkconfig)
    if failures:
        print("Effective M15 sdkconfig check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        print(
            "Use a fresh build directory or reconfigure it from sdkconfig.defaults.",
            file=sys.stderr,
        )
        return 1

    print(f"Effective M15 sdkconfig: PASS ({arguments.sdkconfig})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

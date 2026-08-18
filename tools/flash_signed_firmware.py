#!/usr/bin/env python3
"""Validate and serial-flash the signed M14 application with matching artifacts."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NoReturn

from check_effective_sdkconfig import configuration_failures


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = ROOT / "build-verify"
DEFAULT_SIGNED_IMAGE = ROOT / "smoker_controller.bin"
PUBLIC_KEY = ROOT / "keys/smoker_ota_signing_public.pem"
PARTITION_SIZE = 3 * 1024 * 1024


def fail(message: str) -> NoReturn:
    raise RuntimeError(message)


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        fail(f"{description} is missing: {path}")


def compare_unsigned_prefix(unsigned_image: Path, signed_image: Path) -> None:
    unsigned_size = unsigned_image.stat().st_size
    signed_size = signed_image.stat().st_size
    if signed_size <= unsigned_size:
        fail("signed image has no signature-block growth over the unsigned build image")

    with unsigned_image.open("rb") as unsigned, signed_image.open("rb") as signed:
        while chunk := unsigned.read(1024 * 1024):
            if signed.read(len(chunk)) != chunk:
                fail("signed image does not match the application from this build directory")


def normalized_offset(value: object, description: str) -> int:
    if not isinstance(value, str):
        fail(f"{description} is not a string")
    try:
        return int(value, 0)
    except ValueError:
        fail(f"{description} is not a valid offset: {value}")


def generated_partition_offsets(path: Path) -> dict[str, int]:
    offsets: dict[str, int] = {}
    with path.open(encoding="utf-8", newline="") as source:
        for row in csv.reader(line for line in source if not line.startswith("#")):
            if len(row) >= 4:
                offsets[row[0].strip()] = normalized_offset(
                    row[3].strip(), f"partition {row[0].strip()} offset"
                )
    for required in ("otadata", "ota_0"):
        if required not in offsets:
            fail(f"generated partition table is missing {required}")
    return offsets


def load_flash_command(
    build_dir: Path, signed_image: Path, partition_offsets: dict[str, int]
) -> tuple[list[str], list[tuple[str, Path]]]:
    arguments_file = build_dir / "flasher_args.json"
    require_file(arguments_file, "ESP-IDF flasher arguments")
    try:
        data = json.loads(arguments_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot parse {arguments_file}: {error}")

    extra = data.get("extra_esptool_args")
    write_arguments = data.get("write_flash_args")
    flash_files = data.get("flash_files")
    app = data.get("app")
    if not isinstance(extra, dict) or not isinstance(write_arguments, list):
        fail("flasher_args.json lacks generated ESP-IDF command arguments")
    if not isinstance(flash_files, dict) or not isinstance(app, dict):
        fail("flasher_args.json lacks generated flash-file metadata")

    chip = extra.get("chip")
    before = extra.get("before")
    after = extra.get("after")
    app_offset = app.get("offset")
    app_file = app.get("file")
    if not all(isinstance(value, str) and value for value in (chip, before, after)):
        fail("flasher_args.json has invalid chip/reset metadata")
    if chip != "esp32s3":
        fail(f"generated flash target is {chip}, expected esp32s3")
    if before != "default-reset" or after != "hard-reset":
        fail("generated reset behavior differs from the reviewed M14 serial workflow")
    if not isinstance(app_offset, str) or flash_files.get(app_offset) != app_file:
        fail("flasher_args.json application entry is inconsistent")
    if app.get("encrypted") != "false":
        fail("M14 serial helper does not support an encrypted application entry")
    for required_section in ("bootloader", "partition-table", "otadata"):
        section = data.get(required_section)
        if not isinstance(section, dict):
            fail(f"flasher_args.json is missing the {required_section} entry")
        if section.get("encrypted") != "false":
            fail(f"M14 serial helper does not support encrypted {required_section} data")
        if flash_files.get(section.get("offset")) != section.get("file"):
            fail(f"flasher_args.json has an inconsistent {required_section} entry")

    expected_offsets = {
        "bootloader": 0x0,
        "partition-table": 0x8000,
        "otadata": partition_offsets["otadata"],
        "app": partition_offsets["ota_0"],
    }
    for section_name, expected_offset in expected_offsets.items():
        actual_offset = normalized_offset(
            data[section_name].get("offset"), f"{section_name} flash offset"
        )
        if actual_offset != expected_offset:
            fail(
                f"{section_name} flash offset 0x{actual_offset:x} does not match "
                f"the generated M14 layout at 0x{expected_offset:x}"
            )
    actual_offsets = {
        normalized_offset(offset, "flash-file offset") for offset in flash_files
    }
    if actual_offsets != set(expected_offsets.values()):
        fail("generated flash map contains missing or unexpected M14 write offsets")

    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        chip,
        "--before",
        before,
        "--after",
        after,
    ]
    if extra.get("stub") is False:
        command.append("--no-stub")
    command.append("write-flash")
    if not all(isinstance(value, str) for value in write_arguments):
        fail("flasher_args.json contains a non-string write-flash argument")
    permitted_write_arguments = (
        ["--flash-mode", "dio", "--flash-size", "keep", "--flash-freq", "80m"],
        ["--flash-mode", "dio", "--flash-size", "16MB", "--flash-freq", "80m"],
    )
    if write_arguments not in permitted_write_arguments:
        fail("generated write-flash options differ from the reviewed M14 configuration")
    command.extend(write_arguments)

    flash_map: list[tuple[str, Path]] = []
    for offset, relative_file in flash_files.items():
        if not isinstance(offset, str) or not isinstance(relative_file, str):
            fail("flasher_args.json contains an invalid flash-file entry")
        image = signed_image
        if offset != app_offset:
            image = (build_dir / relative_file).resolve()
            if not image.is_relative_to(build_dir):
                fail(f"flash image escapes the selected build directory: {relative_file}")
        require_file(image, f"flash image at {offset}")
        image = image.resolve()
        command.extend((offset, str(image)))
        flash_map.append((offset, image))
    return command, flash_map


def validate_artifacts(
    build_dir: Path, signed_image: Path
) -> tuple[list[str], list[tuple[str, Path]]]:
    version = subprocess.run(
        ["idf.py", "--version"], capture_output=True, text=True, check=True
    ).stdout.strip()
    if version != "ESP-IDF v6.0.2":
        fail(f"serial flashing requires exactly ESP-IDF v6.0.2, found: {version}")

    sdkconfig = build_dir / "sdkconfig"
    failures = configuration_failures(sdkconfig)
    if failures:
        fail("effective M14 sdkconfig is invalid:\n  - " + "\n  - ".join(failures))

    unsigned_image = build_dir / "smoker_controller.bin"
    partition_binary = build_dir / "partition_table" / "partition-table.bin"
    require_file(unsigned_image, "unsigned application from the verified build")
    require_file(signed_image, "signed application")
    require_file(PUBLIC_KEY, "trusted OTA public key")
    require_file(partition_binary, "generated partition table")
    compare_unsigned_prefix(unsigned_image, signed_image)

    idf_path_text = os.environ.get("IDF_PATH")
    if not idf_path_text:
        fail("IDF_PATH is missing; activate ESP-IDF v6.0.2 first")
    partition_tool = Path(idf_path_text) / "components/partition_table/gen_esp32part.py"
    require_file(partition_tool, "ESP-IDF partition decoder")
    with tempfile.NamedTemporaryFile(suffix=".csv") as generated_csv:
        run(
            [
                sys.executable,
                str(partition_tool),
                "--quiet",
                str(partition_binary),
                generated_csv.name,
            ]
        )
        run([sys.executable, str(ROOT / "tools/check_partitions.py"), generated_csv.name])
        partition_offsets = generated_partition_offsets(Path(generated_csv.name))

    run(
        [
            "idf.py",
            "-B",
            str(build_dir),
            "secure-verify-signature",
            "--keyfile",
            str(PUBLIC_KEY),
            str(signed_image),
        ]
    )
    run(
        [
            sys.executable,
            str(ROOT / "tools/check_firmware_size.py"),
            str(signed_image),
            "--partition-size",
            str(PARTITION_SIZE),
            "--maximum-used-percent",
            "75",
        ]
    )
    return load_flash_command(build_dir, signed_image, partition_offsets)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Validate and flash the signed M14 image with the bootloader, partition "
            "table, and OTA metadata generated by the same ESP-IDF build."
        )
    )
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--signed-image", type=Path, default=DEFAULT_SIGNED_IMAGE)
    parser.add_argument("--port", help="explicit serial port, for example /dev/cu.usbmodemNNN")
    parser.add_argument(
        "--yes",
        action="store_true",
        help="confirm the serial writes (required unless --check-only is used)",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="validate every artifact and print the flash map without writing the board",
    )
    arguments = parser.parse_args()

    if arguments.check_only:
        if arguments.port or arguments.yes:
            parser.error("--check-only cannot be combined with --port or --yes")
    elif not arguments.port or not arguments.yes:
        parser.error("serial flashing requires both --port PORT and --yes")

    try:
        command, flash_map = validate_artifacts(
            arguments.build_dir.resolve(), arguments.signed_image.resolve()
        )
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"Signed M14 serial validation failed: {error}", file=sys.stderr)
        return 1

    print("Validated signed M14 serial image set:")
    for offset, image in flash_map:
        print(f"  {offset}: {image}")
    if arguments.check_only:
        print("Signed M14 serial preflight: PASS (no board was written)")
        return 0

    command[command.index("write-flash"):command.index("write-flash")] = [
        "--port",
        arguments.port,
    ]
    try:
        run(command)
    except subprocess.CalledProcessError as error:
        print(f"Serial flash failed with exit status {error.returncode}", file=sys.stderr)
        return error.returncode or 1
    print("Signed M14 serial flash: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

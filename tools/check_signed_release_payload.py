#!/usr/bin/env python3
"""Bind an ESP Secure Boot v2 signed image to an independent unsigned build."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


SECURE_BOOT_V2_SECTOR_SIZE = 4096


class PayloadError(ValueError):
    """The signed image is not the expected independently built payload."""


def compare_signed_payload(signed_image: Path, expected_unsigned_image: Path) -> None:
    expected_size = expected_unsigned_image.stat().st_size
    signed_size = signed_image.stat().st_size
    if expected_size <= 0:
        raise PayloadError("independent unsigned image must not be empty")
    if expected_size % SECURE_BOOT_V2_SECTOR_SIZE != 0:
        raise PayloadError(
            "independent unsigned image is not ESP Secure Boot v2 sector-aligned"
        )
    if signed_size != expected_size + SECURE_BOOT_V2_SECTOR_SIZE:
        raise PayloadError(
            "signed image must contain the independent payload plus exactly one "
            "ESP Secure Boot v2 signature sector"
        )

    remaining = expected_size
    with (
        signed_image.open("rb") as signed,
        expected_unsigned_image.open("rb") as expected,
    ):
        while remaining:
            chunk_size = min(1024 * 1024, remaining)
            if signed.read(chunk_size) != expected.read(chunk_size):
                raise PayloadError(
                    "RSA-authenticated payload does not match the independent build"
                )
            remaining -= chunk_size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("signed_image", type=Path)
    parser.add_argument("expected_unsigned_image", type=Path)
    arguments = parser.parse_args()
    try:
        compare_signed_payload(
            arguments.signed_image, arguments.expected_unsigned_image
        )
    except (OSError, PayloadError) as error:
        print(f"Signed release payload check failed: {error}", file=sys.stderr)
        return 1
    print(
        "Signed release payload matches the independent reproducible build "
        f"({arguments.expected_unsigned_image.stat().st_size} authenticated bytes)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

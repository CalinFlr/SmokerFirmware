#!/usr/bin/env python3
"""Fail verification when an application image consumes too much of its slot."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--partition-size", type=int, required=True)
    parser.add_argument("--maximum-used-percent", type=float, default=75.0)
    arguments = parser.parse_args()

    image_size = arguments.image.stat().st_size
    used_percent = image_size * 100.0 / arguments.partition_size
    free_bytes = arguments.partition_size - image_size
    print(
        f"Firmware size guard: {image_size} / {arguments.partition_size} bytes "
        f"({used_percent:.1f}% used, {free_bytes} bytes free)"
    )
    if image_size > arguments.partition_size:
        raise SystemExit("firmware image exceeds the application partition")
    if used_percent > arguments.maximum_used_percent:
        raise SystemExit(
            f"firmware image exceeds the {arguments.maximum_used_percent:.1f}% usage limit"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

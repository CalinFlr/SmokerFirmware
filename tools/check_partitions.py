#!/usr/bin/env python3
"""Validate the exact M14 rollback/history partition contract."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


EXPECTED = [
    ("nvs", "data", "nvs", 0x9000, 0x6000),
    ("otadata", "data", "ota", 0xF000, 0x2000),
    ("phy_init", "data", "phy", 0x11000, 0x1000),
    ("ota_0", "app", "ota_0", 0x20000, 0x300000),
    ("ota_1", "app", "ota_1", 0x320000, 0x300000),
    ("history", "data", "0x40", 0x620000, 0x400000),
]


def number(value: str) -> int:
    normalized = value.strip().upper()
    multiplier = 1
    if normalized.endswith("K"):
        normalized = normalized[:-1]
        multiplier = 1024
    elif normalized.endswith("M"):
        normalized = normalized[:-1]
        multiplier = 1024 * 1024
    return int(normalized, 0) * multiplier


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("table", type=Path)
    arguments = parser.parse_args()

    rows: list[tuple[str, str, str, int, int]] = []
    with arguments.table.open(newline="") as source:
        for raw in csv.reader(source):
            if not raw or raw[0].lstrip().startswith("#"):
                continue
            if len(raw) < 5:
                raise SystemExit("partition row has fewer than five columns")
            subtype = raw[2].strip()
            if raw[0].strip() == "history":
                subtype = f"{number(subtype):#x}"
            rows.append(
                (
                    raw[0].strip(),
                    raw[1].strip(),
                    subtype,
                    number(raw[3]),
                    number(raw[4]),
                )
            )

    if rows != EXPECTED:
        raise SystemExit(f"unexpected M14 partition table: {rows!r}")
    for left, right in zip(rows, rows[1:]):
        if left[3] + left[4] > right[3]:
            raise SystemExit(f"overlapping partitions: {left[0]} and {right[0]}")
    if rows[-1][3] + rows[-1][4] > 16 * 1024 * 1024:
        raise SystemExit("partition table exceeds target-confirmed 16 MiB flash")

    unallocated = 16 * 1024 * 1024 - (rows[-1][3] + rows[-1][4])
    if unallocated != 0x5E0000:
        raise SystemExit(f"unexpected unallocated flash: {unallocated:#x}")
    print("M14 partition table: PASS (dual 3 MiB OTA; 4 MiB history; 0x5e0000 free)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

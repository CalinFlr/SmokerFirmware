#!/usr/bin/env python3
"""Verify that every project-owned ESP-IDF C++ source uses strict C++20."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROJECT_SOURCE_ROOTS = (
    ROOT / "components/smoker_core/src",
    ROOT / "components/smoker_app/src",
    ROOT / "components/smoker_platform/src",
    ROOT / "main",
)
CONDITIONAL_SOURCES = {
    (ROOT / "components/smoker_platform/src/max31865_connected_diagnostic.cpp").resolve():
        "CONFIG_SMOKER_MAX31865_CONNECTED_DIAGNOSTIC",
}


def project_sources() -> set[Path]:
    sources: set[Path] = set()
    for source_root in PROJECT_SOURCE_ROOTS:
        sources.update(path.resolve() for path in source_root.glob("*.cpp"))
    return sources


def enabled_configuration_symbols(database_path: Path) -> set[str]:
    sdkconfig_path = database_path.parent / "sdkconfig"
    try:
        lines = sdkconfig_path.read_text().splitlines()
    except OSError as error:
        print(f"could not read {sdkconfig_path}: {error}", file=sys.stderr)
        raise
    return {
        line.removesuffix("=y")
        for line in lines
        if line.startswith("CONFIG_") and line.endswith("=y")
    }


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_target_compile_commands.py COMPILE_COMMANDS", file=sys.stderr)
        return 2

    database_path = Path(sys.argv[1]).resolve()
    try:
        entries = json.loads(database_path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read {database_path}: {error}", file=sys.stderr)
        return 1

    try:
        enabled_symbols = enabled_configuration_symbols(database_path)
    except OSError:
        return 1

    expected = project_sources()
    for source, symbol in CONDITIONAL_SOURCES.items():
        if symbol not in enabled_symbols:
            expected.discard(source)
    observed: set[Path] = set()
    failures: list[str] = []
    for entry in entries:
        source = Path(entry.get("file", "")).resolve()
        if source not in expected:
            continue

        observed.add(source)
        command = entry.get("command", "")
        standard_flags = re.findall(r"(?:^|\s)(-std=[^\s]+)", command)
        if not standard_flags or standard_flags[-1] != "-std=c++20":
            relative = source.relative_to(ROOT)
            failures.append(
                f"{relative} effective language flag is "
                f"{standard_flags[-1] if standard_flags else 'missing'}, expected -std=c++20"
            )

    for source in sorted(expected - observed):
        failures.append(f"{source.relative_to(ROOT)} is absent from the target compile database")

    if failures:
        print("Target C++ standard check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"Target C++ standard: PASS (strict C++20, {len(observed)} project sources)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

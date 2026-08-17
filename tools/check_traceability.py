#!/usr/bin/env python3
"""Validate complete, unique, and test-backed requirements traceability."""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DOCUMENTS = (
    ROOT / "docs/BUSINESS_RULES.md",
    ROOT / "docs/SAFETY.md",
)
TRACEABILITY = ROOT / "docs/TRACEABILITY.md"
RULE_PATTERN = re.compile(r"^###\s+([A-Z]+-\d{3})\b", re.MULTILINE)
ROW_PATTERN = re.compile(r"^\|\s*([A-Z]+-\d{3})\s*\|(.*)\|\s*$", re.MULTILINE)
TEST_PATTERN = re.compile(r"\bvoid\s+(test_[a-zA-Z0-9_]+)\s*\(")
TEST_REFERENCE_PATTERN = re.compile(r"`(test_[a-zA-Z0-9_]+)`")
VALIDATION_MARKERS = (
    "H-pass",
    "B-pass",
    "T-pending",
    "HW-pending",
    "Guardrail",
    "Deferred",
)


def fail(messages: list[str]) -> int:
    print("Traceability guardrails failed:", file=sys.stderr)
    for message in messages:
        print(f"  - {message}", file=sys.stderr)
    return 1


def main() -> int:
    messages: list[str] = []
    source_ids: list[str] = []
    for path in SOURCE_DOCUMENTS:
        source_ids.extend(RULE_PATTERN.findall(path.read_text()))

    source_counts = Counter(source_ids)
    for rule_id, count in sorted(source_counts.items()):
        if count != 1:
            messages.append(f"source rule {rule_id} is declared {count} times")

    trace_text = TRACEABILITY.read_text()
    rows = ROW_PATTERN.findall(trace_text)
    row_counts = Counter(rule_id for rule_id, _ in rows)
    expected = set(source_counts)
    actual = set(row_counts)
    for rule_id in sorted(expected - actual):
        messages.append(f"source rule {rule_id} has no explicit traceability row")
    for rule_id in sorted(actual - expected):
        messages.append(f"traceability row {rule_id} has no source rule")
    for rule_id, count in sorted(row_counts.items()):
        if count != 1:
            messages.append(f"traceability rule {rule_id} has {count} rows; expected exactly one")

    test_text = "\n".join(path.read_text() for path in (ROOT / "tests").rglob("*.cpp"))
    existing_tests = set(TEST_PATTERN.findall(test_text))
    referenced_tests: set[str] = set()

    for rule_id, remainder in rows:
        cells = [cell.strip() for cell in remainder.split("|")]
        if len(cells) != 4 or any(not cell for cell in cells):
            messages.append(f"{rule_id} must have four non-empty evidence/status cells")
            continue
        milestone, _implementation, test_evidence, validation = cells
        if not any(marker in validation for marker in VALIDATION_MARKERS):
            messages.append(f"{rule_id} has no recognized validation marker")

        test_references = set(TEST_REFERENCE_PATTERN.findall(test_evidence))
        referenced_tests.update(test_references)
        for test_name in sorted(test_references - existing_tests):
            messages.append(f"{rule_id} references missing host test {test_name}")
        for test_name in sorted(test_references & existing_tests):
            call_count = len(re.findall(rf"\b{re.escape(test_name)}\s*\(", test_text))
            if call_count < 2:
                messages.append(
                    f"{rule_id} references host test {test_name}, but it is not invoked"
                )

        if "implemented" in milestone.lower():
            if not test_references:
                messages.append(f"implemented rule {rule_id} must reference a concrete `test_*` function")
            if "H-pass" not in validation:
                messages.append(f"implemented rule {rule_id} must declare H-pass validation")

    if messages:
        return fail(messages)

    print(
        "Traceability guardrails: PASS "
        f"({len(expected)} rules, {len(referenced_tests)} referenced host tests)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

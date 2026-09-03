#!/usr/bin/env python3
"""Fail closed when the release workflow loses its privilege boundaries."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/release.yml"
EXPECTED_JOBS = (
    "validate",
    "sign",
    "verify-signed-artifact",
    "publish",
)
ACTION_RE = re.compile(
    r"^\s*-?\s*uses:\s*([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)@([0-9a-f]{40})"
    r"\s+#\s+(v[^\s]+)\s*$"
)
OFFICIAL_ACTIONS = frozenset(
    {
        "actions/checkout",
        "actions/download-artifact",
        "actions/upload-artifact",
        "espressif/esp-idf-ci-action",
    }
)


def job_blocks(source: str) -> dict[str, str]:
    jobs_start = source.index("\njobs:\n") + len("\njobs:\n")
    jobs_source = source[jobs_start:]
    matches = list(
        re.finditer(r"^  ([a-z][a-z0-9-]*):\s*$", jobs_source, re.MULTILINE)
    )
    blocks: dict[str, str] = {}
    for index, match in enumerate(matches):
        end = (
            matches[index + 1].start()
            if index + 1 < len(matches)
            else len(jobs_source)
        )
        blocks[match.group(1)] = jobs_source[match.start() : end]
    return blocks


def named_step(job: str, name_prefix: str) -> str:
    starts = list(re.finditer(r"^      - name:\s*(.+)$", job, re.MULTILINE))
    for index, match in enumerate(starts):
        if match.group(1).startswith(name_prefix):
            end = starts[index + 1].start() if index + 1 < len(starts) else len(job)
            return job[match.start() : end]
    return ""


def check_workflow(source: str) -> list[str]:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    try:
        jobs = job_blocks(source)
    except ValueError:
        return ["workflow must contain a top-level jobs mapping"]
    require(tuple(jobs) == EXPECTED_JOBS, "job graph must contain only validate, sign, verify-signed-artifact, publish in order")
    if tuple(jobs) != EXPECTED_JOBS:
        return failures

    pre_jobs = source[: source.index("jobs:")]
    require(
        re.search(r"^permissions:\s*\n  contents: read\s*$", pre_jobs, re.MULTILINE)
        is not None,
        "top-level permissions must be contents: read",
    )
    require("contents: write" not in pre_jobs, "top-level permissions must not grant contents: write")

    validate = jobs["validate"]
    sign = jobs["sign"]
    verify = jobs["verify-signed-artifact"]
    publish = jobs["publish"]

    require(re.search(r"^    needs:\s*validate\s*$", sign, re.MULTILINE) is not None, "sign must need validate")
    require(
        re.search(r"^    needs:\s*sign\s*$", verify, re.MULTILINE) is not None,
        "verify-signed-artifact must need sign",
    )
    require(
        re.search(r"^    needs:\s*verify-signed-artifact\s*$", publish, re.MULTILINE)
        is not None,
        "publish must need verify-signed-artifact",
    )

    for name, block in jobs.items():
        expected_permission = "write" if name == "publish" else "read"
        require(
            re.search(
                rf"^    permissions:\s*\n      contents: {expected_permission}\s*$",
                block,
                re.MULTILINE,
            )
            is not None,
            f"{name} must explicitly use contents: {expected_permission}",
        )
        checkout_steps = re.findall(
            r"^      - uses: actions/checkout@[0-9a-f]{40}.*?(?=^      - |\Z)",
            block,
            re.MULTILINE | re.DOTALL,
        )
        require(len(checkout_steps) == 1, f"{name} must have exactly one pinned checkout")
        if checkout_steps:
            checkout = checkout_steps[0]
            require("ref: ${{ github.sha }}" in checkout, f"{name} checkout must use exact github.sha")
            require("persist-credentials: false" in checkout, f"{name} checkout must disable credential persistence")

    require(source.count("contents: write") == 1, "only publish may receive contents: write")
    require("contents: write" in publish, "publish must receive contents: write")
    require("contents: write" not in sign, "sign must not receive contents: write")

    require(source.count("environment: firmware-release") == 1, "firmware-release environment must appear exactly once")
    require("environment: firmware-release" in sign, "firmware-release environment must belong only to sign")

    signing_step = named_step(sign, "Sign and verify")
    require(bool(signing_step), "sign must contain a named Sign and verify step")
    require(source.count("secrets.SMOKER_OTA_SIGNING_KEY_B64") == 1, "signing secret expression must appear exactly once")
    for line in source.splitlines():
        if "SMOKER_OTA_SIGNING_KEY_B64" in line:
            require(line in signing_step.splitlines(), "every signing-secret reference must stay inside the signing step")
    require("SMOKER_OTA_SIGNING_KEY_B64" not in publish, "publish must not contain the signing secret")
    require("SMOKER_OTA_SIGNING_KEY_B64" not in validate, "validate must not contain the signing secret")
    require("SMOKER_OTA_SIGNING_KEY_B64" not in verify, "independent verification must not contain the signing secret")

    require(source.count("gh release create") == 1, "gh release create must appear exactly once")
    require("gh release create" in publish, "gh release create must belong only to publish")
    require("secure-sign-data" not in publish, "publish must never sign")
    require("esp-idf-ci-action" not in publish, "publish must never rebuild firmware")
    require("tools/sign_release_firmware.sh" not in publish, "publish must never invoke the signer")
    require("tools/verify.sh" not in publish, "publish must never run a build validation entrypoint")
    require("actions/upload-artifact@" not in validate, "validate must not produce a release artifact")
    require("tools/release_bundle.py verify" in verify, "independent verification must verify the strict bundle")
    require("tools/verify_signed_release_firmware.sh" in verify, "independent verification must repeat public-key RSA verification")
    require("tools/release_bundle.py verify" in publish, "publish must reverify hash and manifest")
    require("gh release view" in publish, "publish must refuse an existing release")
    require("--verify-tag" in publish, "publish must require the triggering tag to exist")
    require("smoker_controller.bin" in source, "fixed OTA image name must remain smoker_controller.bin")
    require("smoker_controller.manifest.json" in publish, "publish must allowlist the versioned manifest")
    require("--clobber" not in publish, "publish must never overwrite release assets")
    require("git tag" not in source and "git push" not in source, "release workflow must never create or move tags")

    uses_lines = [line for line in source.splitlines() if re.match(r"^\s*-?\s*uses:", line)]
    for line in uses_lines:
        match = ACTION_RE.fullmatch(line)
        require(match is not None, f"action must use a full SHA with a human version comment: {line.strip()}")
        if match is not None:
            require(match.group(1) in OFFICIAL_ACTIONS, f"workflow action is not on the reviewed official allowlist: {match.group(1)}")
    require("actions/upload-artifact@" in sign and "actions/upload-artifact@" in verify, "sign and verification must use official artifact upload")
    require("actions/download-artifact@" in verify and "actions/download-artifact@" in publish, "verification and publish must use official artifact download")
    require("persist-credentials: true" not in source, "credential persistence must never be enabled")
    return failures


def main() -> int:
    failures = check_workflow(WORKFLOW.read_text(encoding="utf-8"))
    if failures:
        print("Release workflow guardrail failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Release workflow privilege and pinning guardrail: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

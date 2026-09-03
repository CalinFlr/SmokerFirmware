#!/usr/bin/env python3
"""Fail closed when the release workflow loses its privilege boundaries."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


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

PSYCH_AST_TO_JSON = r"""
require "json"
require "psych"

def convert(node)
  if node.respond_to?(:anchor) && node.anchor
    raise "YAML anchors are not allowed"
  end
  if node.respond_to?(:tag) && node.tag
    raise "explicit YAML tags are not allowed"
  end
  case node
  when Psych::Nodes::Stream, Psych::Nodes::Document
    raise "YAML must contain exactly one document" unless node.children.length == 1
    convert(node.children.fetch(0))
  when Psych::Nodes::Mapping
    result = {}
    node.children.each_slice(2) do |key_node, value_node|
      raise "YAML mapping keys must be scalars" unless key_node.is_a?(Psych::Nodes::Scalar)
      key = key_node.value
      raise "duplicate YAML mapping key: #{key}" if result.key?(key)
      result[key] = convert(value_node)
    end
    result
  when Psych::Nodes::Sequence
    node.children.map { |child| convert(child) }
  when Psych::Nodes::Scalar
    node.value
  when Psych::Nodes::Alias
    raise "YAML aliases are not allowed"
  else
    raise "unsupported YAML node: #{node.class}"
  end
end

begin
  print JSON.generate(convert(Psych.parse_stream(STDIN.read)))
rescue StandardError => error
  warn error.message
  exit 1
end
"""


class WorkflowParseError(ValueError):
    """The workflow is not a safe, unambiguous YAML mapping."""


def parse_workflow(source: str) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            ["ruby", "-e", PSYCH_AST_TO_JSON],
            input=source,
            text=True,
            capture_output=True,
            check=False,
        )
    except FileNotFoundError as error:
        raise WorkflowParseError(
            "Ruby/Psych is required for semantic YAML validation"
        ) from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "unknown Psych parse error"
        raise WorkflowParseError(detail)
    try:
        parsed = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise WorkflowParseError(f"Psych result is not valid JSON: {error}") from error
    if not isinstance(parsed, dict):
        raise WorkflowParseError("workflow root must be a mapping")
    return parsed


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
        workflow = parse_workflow(source)
    except WorkflowParseError as error:
        return [f"workflow YAML is invalid or ambiguous: {error}"]

    require(
        set(workflow) == {"name", "on", "permissions", "concurrency", "jobs"},
        "workflow must contain only name, on, permissions, concurrency, and jobs",
    )
    require(
        workflow.get("on") == {"push": {"tags": ["v*.*.*"]}},
        "workflow trigger must be exactly push tags v*.*.*",
    )
    require(
        workflow.get("permissions") == {"contents": "read"},
        "top-level permissions must be exactly contents: read",
    )
    require(
        workflow.get("concurrency")
        == {"group": "release-${{ github.ref }}", "cancel-in-progress": "true"},
        "same-tag release runs must cancel an older in-progress run",
    )

    semantic_jobs = workflow.get("jobs")
    if not isinstance(semantic_jobs, dict):
        failures.append("workflow jobs must be a mapping")
        return failures
    require(
        tuple(semantic_jobs) == EXPECTED_JOBS,
        "semantic job graph must contain only validate, sign, "
        "verify-signed-artifact, publish in order",
    )
    if tuple(semantic_jobs) != EXPECTED_JOBS:
        return failures

    allowed_job_keys = {
        "validate": {"name", "runs-on", "permissions", "timeout-minutes", "steps"},
        "sign": {
            "name",
            "needs",
            "runs-on",
            "environment",
            "permissions",
            "timeout-minutes",
            "steps",
        },
        "verify-signed-artifact": {
            "name",
            "needs",
            "runs-on",
            "permissions",
            "timeout-minutes",
            "steps",
        },
        "publish": {
            "name",
            "needs",
            "runs-on",
            "permissions",
            "timeout-minutes",
            "steps",
        },
    }
    expected_needs = {
        "validate": None,
        "sign": "validate",
        "verify-signed-artifact": "sign",
        "publish": "verify-signed-artifact",
    }
    expected_step_names = {
        "validate": (
            None,
            "Verify tag matches version.txt exactly",
            "Install host build tools",
            "Validate host behavior, sanitizers, and guardrails",
            "Cross-build with ESP-IDF v6.0.2",
        ),
        "sign": (
            None,
            "Build signed source again from tagged commit",
            "Sign and verify with the release key",
            "Create bounded release bundle",
            "Upload signed release bundle",
        ),
        "verify-signed-artifact": (
            None,
            "Download signed release bundle",
            "Verify bundle manifest, identity, hash, size, and file set",
            "Repeat RSA verification using only the public key",
            "Upload independently verified bundle",
        ),
        "publish": (
            None,
            "Download independently verified bundle",
            "Reverify bundle and publish approved files",
        ),
    }
    for name, job in semantic_jobs.items():
        if not isinstance(job, dict):
            failures.append(f"{name} job must be a mapping")
            continue
        require(
            set(job) == allowed_job_keys[name],
            f"{name} job keys must be exact; conditions, continue-on-error, "
            "and alternate execution forms are forbidden",
        )
        require(job.get("needs") == expected_needs[name], f"{name} needs must be exact")
        expected_permission = "write" if name == "publish" else "read"
        require(
            job.get("permissions") == {"contents": expected_permission},
            f"{name} permissions must be exactly contents: {expected_permission}",
        )
        expected_environment = "firmware-release" if name == "sign" else None
        require(
            job.get("environment") == expected_environment,
            f"{name} release environment assignment must be exact",
        )
        steps = job.get("steps")
        if not isinstance(steps, list):
            failures.append(f"{name} steps must be a sequence")
            continue
        require(
            tuple(
                step.get("name") if isinstance(step, dict) else None
                for step in steps
            )
            == expected_step_names[name],
            f"{name} steps must be the exact reviewed sequence",
        )
        for step in steps:
            if not isinstance(step, dict):
                failures.append(f"{name} step must be a mapping")
                continue
            require(
                set(step) <= {"name", "uses", "with", "env", "run", "shell"},
                f"{name} steps must not use conditions, continue-on-error, or unknown keys",
            )

        checkout_steps = [
            step
            for step in steps
            if isinstance(step, dict)
            and isinstance(step.get("uses"), str)
            and step["uses"].startswith("actions/checkout@")
        ]
        require(len(checkout_steps) == 1, f"{name} must have exactly one semantic checkout")
        if len(checkout_steps) == 1:
            checkout_with = checkout_steps[0].get("with")
            require(
                checkout_with
                == {
                    "ref": "${{ github.sha }}",
                    "fetch-depth": "0",
                    "persist-credentials": "false",
                },
                f"{name} checkout inputs must pin github.sha, fetch full tag "
                "history, and disable credential persistence",
            )

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
    require("github.event.created" in validate, "validate must reject tag-update events")
    require("gh release view" in publish, "publish must refuse an existing release")
    require(
        "gh api" in publish and "commits/$GITHUB_REF_NAME" in publish,
        "publish must resolve the live release tag commit",
    )
    require(
        "$live_tag_commit" in publish and '"$GITHUB_SHA"' in publish,
        "publish must compare the live tag commit with github.sha",
    )
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

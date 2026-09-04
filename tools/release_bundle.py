#!/usr/bin/env python3
"""Create and strictly verify the bounded firmware release bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path
from typing import Any, NoReturn


SCHEMA = "smoker-firmware-release/v1"
IMAGE_NAME = "smoker_controller.bin"
CHECKSUM_NAME = f"{IMAGE_NAME}.sha256"
MANIFEST_NAME = "smoker_controller.manifest.json"
EXPECTED_FILES = frozenset({IMAGE_NAME, CHECKSUM_NAME, MANIFEST_NAME})
OTA_SLOT_SIZE = 3 * 1024 * 1024
MANIFEST_MAX_BYTES = 1024
MANIFEST_FIELDS = frozenset(
    {"schema", "version", "tag", "commit", "image", "sha256", "size"}
)
SEMVER_RE = re.compile(
    r"^(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)\."
    r"(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class BundleError(ValueError):
    """A release bundle violates its strict contract."""


def fail(message: str) -> NoReturn:
    raise BundleError(message)


def validate_semver(version: str) -> None:
    if not isinstance(version, str) or SEMVER_RE.fullmatch(version) is None:
        fail(f"version is not strict SemVer: {version!r}")


def validate_identity(version: str, tag: str, commit: str) -> None:
    validate_semver(version)
    if tag != f"v{version}":
        fail(f"tag {tag!r} does not exactly match version {version!r}")
    if COMMIT_RE.fullmatch(commit) is None:
        fail("commit must be an exact lowercase 40-character Git SHA")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_manifest_bytes(manifest: dict[str, Any]) -> bytes:
    return (
        json.dumps(
            manifest,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"manifest contains duplicate field: {key}")
        result[key] = value
    return result


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > MANIFEST_MAX_BYTES:
            fail("manifest exceeds its bounded size")
        raw = path.read_bytes()
        parsed = json.loads(
            raw,
            object_pairs_hook=reject_duplicate_keys,
            parse_constant=lambda value: fail(
                f"manifest contains invalid numeric constant: {value}"
            ),
        )
    except BundleError:
        raise
    except (OSError, UnicodeDecodeError, ValueError) as error:
        fail(f"manifest is invalid JSON: {error}")
    if not isinstance(parsed, dict):
        fail("manifest root must be an object")
    if frozenset(parsed) != MANIFEST_FIELDS:
        missing = sorted(MANIFEST_FIELDS - frozenset(parsed))
        unknown = sorted(frozenset(parsed) - MANIFEST_FIELDS)
        fail(f"manifest fields are not exact (missing={missing}, unknown={unknown})")
    if raw != canonical_manifest_bytes(parsed):
        fail("manifest is not in canonical deterministic form")
    return parsed


def manifest_for(image: Path, version: str, tag: str, commit: str) -> dict[str, Any]:
    validate_identity(version, tag, commit)
    if image.name != IMAGE_NAME:
        fail(f"release image must be named exactly {IMAGE_NAME}")
    if not image.is_file() or image.is_symlink():
        fail(f"release image is missing or not a regular file: {image}")
    size = image.stat().st_size
    if size <= 0:
        fail("release image must not be empty")
    if size > OTA_SLOT_SIZE:
        fail(f"release image size {size} exceeds OTA slot size {OTA_SLOT_SIZE}")
    return {
        "commit": commit,
        "image": IMAGE_NAME,
        "schema": SCHEMA,
        "sha256": sha256_file(image),
        "size": size,
        "tag": tag,
        "version": version,
    }


def create_bundle(
    source_image: Path, output_dir: Path, version: str, tag: str, commit: str
) -> dict[str, Any]:
    if output_dir.exists():
        if not output_dir.is_dir() or any(output_dir.iterdir()):
            fail(f"bundle output directory must be absent or empty: {output_dir}")
    else:
        output_dir.mkdir(parents=True)

    destination = output_dir / IMAGE_NAME
    if source_image.resolve() == destination.resolve():
        fail("source image must be outside the bundle output directory")
    if source_image.name != IMAGE_NAME:
        fail(f"release image must be named exactly {IMAGE_NAME}")
    shutil.copyfile(source_image, destination)
    manifest = manifest_for(destination, version, tag, commit)
    (output_dir / CHECKSUM_NAME).write_bytes(
        f"{manifest['sha256']}  {IMAGE_NAME}\n".encode("ascii")
    )
    (output_dir / MANIFEST_NAME).write_bytes(canonical_manifest_bytes(manifest))
    return manifest


def verify_bundle(
    bundle_dir: Path, expected_version: str, expected_tag: str, expected_commit: str
) -> dict[str, Any]:
    validate_identity(expected_version, expected_tag, expected_commit)
    if not bundle_dir.is_dir() or bundle_dir.is_symlink():
        fail(f"bundle directory is missing or invalid: {bundle_dir}")
    entries = {entry.name: entry for entry in bundle_dir.iterdir()}
    if frozenset(entries) != EXPECTED_FILES:
        missing = sorted(EXPECTED_FILES - frozenset(entries))
        unexpected = sorted(frozenset(entries) - EXPECTED_FILES)
        fail(f"bundle files are not exact (missing={missing}, unexpected={unexpected})")
    for name, path in entries.items():
        if not path.is_file() or path.is_symlink():
            fail(f"bundle member is not a regular file: {name}")

    manifest = load_manifest(entries[MANIFEST_NAME])
    if manifest["schema"] != SCHEMA:
        fail(f"unsupported manifest schema: {manifest['schema']!r}")
    if manifest["version"] != expected_version:
        fail("manifest version does not match the expected release version")
    if manifest["tag"] != expected_tag:
        fail("manifest tag does not match the triggering tag")
    if manifest["commit"] != expected_commit:
        fail("manifest commit does not match the triggering commit")
    validate_identity(manifest["version"], manifest["tag"], manifest["commit"])
    if manifest["image"] != IMAGE_NAME:
        fail(f"manifest image must be named exactly {IMAGE_NAME}")
    if not isinstance(manifest["sha256"], str) or SHA256_RE.fullmatch(
        manifest["sha256"]
    ) is None:
        fail("manifest SHA-256 is invalid")
    if type(manifest["size"]) is not int or manifest["size"] <= 0:
        fail("manifest image size must be a positive integer")
    if manifest["size"] > OTA_SLOT_SIZE:
        fail("manifest image size exceeds the OTA slot")

    image = entries[IMAGE_NAME]
    actual_size = image.stat().st_size
    if actual_size != manifest["size"]:
        fail("image size does not match the manifest")
    actual_hash = sha256_file(image)
    if actual_hash != manifest["sha256"]:
        fail("image SHA-256 does not match the manifest")
    expected_sidecar = f"{actual_hash}  {IMAGE_NAME}\n"
    try:
        sidecar = entries[CHECKSUM_NAME].read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as error:
        fail(f"SHA-256 sidecar is invalid: {error}")
    if sidecar != expected_sidecar:
        fail("SHA-256 sidecar does not exactly match the image and manifest")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    create = subparsers.add_parser("create", help="create a strict release bundle")
    create.add_argument("--image", type=Path, required=True)
    create.add_argument("--output-dir", type=Path, required=True)
    verify = subparsers.add_parser("verify", help="verify a strict release bundle")
    verify.add_argument("--bundle-dir", type=Path, required=True)
    for command in (create, verify):
        command.add_argument("--version", required=True)
        command.add_argument("--tag", required=True)
        command.add_argument("--commit", required=True)
    arguments = parser.parse_args()
    try:
        if arguments.operation == "create":
            manifest = create_bundle(
                arguments.image,
                arguments.output_dir,
                arguments.version,
                arguments.tag,
                arguments.commit,
            )
            print(
                f"Release bundle created: {arguments.output_dir} "
                f"({manifest['sha256']}, {manifest['size']} bytes)"
            )
        else:
            manifest = verify_bundle(
                arguments.bundle_dir,
                arguments.version,
                arguments.tag,
                arguments.commit,
            )
            print(
                f"Release bundle verified: {arguments.bundle_dir} "
                f"({manifest['sha256']}, {manifest['size']} bytes)"
            )
    except (BundleError, OSError) as error:
        print(f"Release bundle validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

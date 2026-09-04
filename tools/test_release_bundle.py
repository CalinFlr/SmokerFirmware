#!/usr/bin/env python3
"""Executable no-network tests for the strict release bundle."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from release_bundle import (
    BundleError,
    CHECKSUM_NAME,
    IMAGE_NAME,
    MANIFEST_NAME,
    OTA_SLOT_SIZE,
    SCHEMA,
    canonical_manifest_bytes,
    create_bundle,
    validate_semver,
    verify_bundle,
)


ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.16.0"
TAG = "v0.16.0"
COMMIT = "0123456789abcdef0123456789abcdef01234567"


class ReleaseBundleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.image = self.root / IMAGE_NAME
        self.image.write_bytes(b"signed-esp32s3-firmware\x00\x01")
        self.bundle = self.root / "bundle"
        create_bundle(self.image, self.bundle, VERSION, TAG, COMMIT)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def assert_rejected(self) -> None:
        with self.assertRaises(BundleError):
            verify_bundle(self.bundle, VERSION, TAG, COMMIT)

    def rewrite_manifest(self, **changes: object) -> None:
        path = self.bundle / MANIFEST_NAME
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest.update(changes)
        path.write_bytes(canonical_manifest_bytes(manifest))

    def test_valid_manifest_and_bundle(self) -> None:
        manifest = verify_bundle(self.bundle, VERSION, TAG, COMMIT)
        self.assertEqual(manifest["version"], VERSION)
        self.assertEqual(manifest["tag"], TAG)
        self.assertEqual(manifest["commit"], COMMIT)

    def test_image_hash_mismatch(self) -> None:
        (self.bundle / IMAGE_NAME).write_bytes(b"tampered image")
        self.assert_rejected()

    def test_manifest_hash_mismatch(self) -> None:
        self.rewrite_manifest(sha256="f" * 64)
        (self.bundle / CHECKSUM_NAME).write_text(
            f"{'f' * 64}  {IMAGE_NAME}\n", encoding="ascii"
        )
        self.assert_rejected()

    def test_wrong_version(self) -> None:
        self.rewrite_manifest(version="0.16.1", tag="v0.16.1")
        self.assert_rejected()

    def test_wrong_tag(self) -> None:
        self.rewrite_manifest(tag="v0.16.1")
        self.assert_rejected()

    def test_wrong_commit(self) -> None:
        self.rewrite_manifest(commit="f" * 40)
        self.assert_rejected()

    def test_wrong_schema(self) -> None:
        self.rewrite_manifest(schema=f"{SCHEMA}-unknown")
        self.assert_rejected()

    def test_invalid_declared_sizes(self) -> None:
        actual_size = (self.bundle / IMAGE_NAME).stat().st_size
        for invalid_size in (True, 0, -1, str(actual_size), actual_size + 1):
            with self.subTest(size=invalid_size):
                self.rewrite_manifest(size=invalid_size)
                self.assert_rejected()

    def test_declared_size_beyond_ota_slot(self) -> None:
        self.rewrite_manifest(size=OTA_SLOT_SIZE + 1)
        self.assert_rejected()

    def test_create_rejects_image_beyond_ota_slot(self) -> None:
        self.image.write_bytes(b"x" * (OTA_SLOT_SIZE + 1))
        with self.assertRaises(BundleError):
            create_bundle(
                self.image,
                self.root / "oversized-bundle",
                VERSION,
                TAG,
                COMMIT,
            )

    def test_checksum_sidecar_mismatch(self) -> None:
        (self.bundle / CHECKSUM_NAME).write_text(
            f"{'e' * 64}  {IMAGE_NAME}\n", encoding="ascii"
        )
        self.assert_rejected()

    def test_missing_file(self) -> None:
        (self.bundle / CHECKSUM_NAME).unlink()
        self.assert_rejected()

    def test_unexpected_file(self) -> None:
        (self.bundle / "unapproved.txt").write_text("no", encoding="utf-8")
        self.assert_rejected()

    def test_wrong_image_name(self) -> None:
        self.rewrite_manifest(image="other.bin")
        self.assert_rejected()

    def test_deterministic_manifest_output(self) -> None:
        second = self.root / "second"
        create_bundle(self.image, second, VERSION, TAG, COMMIT)
        self.assertEqual(
            (self.bundle / MANIFEST_NAME).read_bytes(),
            (second / MANIFEST_NAME).read_bytes(),
        )
        self.assertEqual(
            (self.bundle / CHECKSUM_NAME).read_bytes(),
            (second / CHECKSUM_NAME).read_bytes(),
        )

    def test_strict_semver(self) -> None:
        for valid in (
            "0.16.0",
            "1.0.0-alpha",
            "1.0.0-alpha.1",
            "1.0.0+build.7",
        ):
            validate_semver(valid)
        for invalid in (
            "v0.16.0",
            "0.16",
            "01.16.0",
            "0.016.0",
            "0.16.00",
            "0.16.0-01",
            "0.16.0-",
            "0.16.0+",
            " 0.16.0",
            "0.16.0\n",
        ):
            with self.subTest(version=invalid), self.assertRaises(BundleError):
                validate_semver(invalid)

    def test_version_txt_is_exact(self) -> None:
        self.assertEqual((ROOT / "version.txt").read_bytes(), b"0.16.0\n")

    def test_missing_manifest_field(self) -> None:
        path = self.bundle / MANIFEST_NAME
        manifest = json.loads(path.read_text(encoding="utf-8"))
        del manifest["size"]
        path.write_bytes(canonical_manifest_bytes(manifest))
        self.assert_rejected()

    def test_unknown_manifest_field(self) -> None:
        path = self.bundle / MANIFEST_NAME
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["extra"] = "forbidden"
        path.write_bytes(canonical_manifest_bytes(manifest))
        self.assert_rejected()

    def test_invalid_manifest(self) -> None:
        (self.bundle / MANIFEST_NAME).write_text("{not-json}\n", encoding="utf-8")
        self.assert_rejected()

    def test_duplicate_manifest_field(self) -> None:
        path = self.bundle / MANIFEST_NAME
        raw = path.read_text(encoding="utf-8")
        path.write_text(
            raw.replace(
                '{"commit":',
                '{"commit":"' + ("f" * 40) + '","commit":',
                1,
            ),
            encoding="utf-8",
        )
        self.assert_rejected()

    def test_symlinked_bundle_members(self) -> None:
        for name in (IMAGE_NAME, CHECKSUM_NAME, MANIFEST_NAME):
            with self.subTest(name=name):
                path = self.bundle / name
                original = path.read_bytes()
                target = self.root / f"{name}.target"
                target.write_bytes(original)
                path.unlink()
                path.symlink_to(target)
                self.assert_rejected()
                path.unlink()
                path.write_bytes(original)

    def test_oversized_manifest(self) -> None:
        (self.bundle / MANIFEST_NAME).write_bytes(b" " * 1025)
        self.assert_rejected()


if __name__ == "__main__":
    unittest.main(verbosity=2)

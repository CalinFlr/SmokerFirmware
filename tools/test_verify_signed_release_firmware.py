#!/usr/bin/env python3
"""Exercise independent payload binding and ESP-IDF 6.0.2 cwd semantics."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERIFY_SCRIPT = ROOT / "tools" / "verify_signed_release_firmware.sh"
EXPECTED_IDF_VERSION = "ESP-IDF v6.0.2"
SECTOR_SIZE = 4096
UNSIGNED_BYTES = b"independent-release-payload\x00\x01\x02".ljust(
    SECTOR_SIZE, b"\xff"
)
SIGNATURE_SECTOR = (b"\xe7\x02" + b"fake-rsa-signature").ljust(
    SECTOR_SIZE, b"\xff"
)
SIGNED_BYTES = UNSIGNED_BYTES + SIGNATURE_SECTOR

FAKE_IDF = r"""#!/usr/bin/env python3
import json
import os
import sys
from pathlib import Path

arguments = sys.argv[1:]
if arguments == ["--version"]:
    print(os.environ.get("FAKE_IDF_VERSION", "ESP-IDF v6.0.2"))
    raise SystemExit(0)

if "secure-verify-signature" not in arguments:
    raise SystemExit(40)
try:
    build_index = arguments.index("-B")
    build_argument = arguments[build_index + 1]
except (ValueError, IndexError):
    raise SystemExit(40)

launch_cwd = Path.cwd()
build_dir = Path(build_argument)
if not build_dir.is_absolute():
    build_dir = (launch_cwd / build_dir).resolve()
build_dir.mkdir(parents=True, exist_ok=True)
os.chdir(build_dir)

datafile = arguments[-1]
record = {
    "arguments": arguments,
    "build_argument": build_argument,
    "datafile": datafile,
    "datafile_is_absolute": Path(datafile).is_absolute(),
    "launch_cwd": str(launch_cwd),
    "tool_cwd": str(Path.cwd()),
}
Path(os.environ["FAKE_IDF_LOG"]).write_text(
    json.dumps(record, sort_keys=True), encoding="utf-8"
)

if not Path(datafile).is_absolute():
    raise SystemExit(41)
if not Path(datafile).is_file():
    raise SystemExit(42)
"""


class VerifySignedReleaseFirmwareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.fixture_root = Path(self.temporary.name)
        self.fake_bin = self.fixture_root / "fake bin"
        self.fake_bin.mkdir()
        self.fake_idf = self.fake_bin / "idf.py"
        self.fake_idf.write_text(FAKE_IDF, encoding="utf-8")
        self.fake_idf.chmod(0o755)
        self.build_dir = self.fixture_root / "build output"
        self.log = self.fixture_root / "idf invocation.json"
        self.environment = os.environ.copy()
        self.environment.update(
            {
                "FAKE_IDF_LOG": str(self.log),
                "FAKE_IDF_VERSION": EXPECTED_IDF_VERSION,
                "PATH": f"{self.fake_bin}{os.pathsep}{self.environment['PATH']}",
                "SMOKER_RELEASE_BUILD_DIR": str(self.build_dir),
            }
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def create_file(self, relative_path: str, contents: bytes) -> Path:
        path = self.fixture_root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def create_pair(
        self,
        signed_path: str = "signed-release-bundle/smoker_controller.bin",
        unsigned_path: str = "independent-build/smoker_controller.bin",
    ) -> tuple[Path, Path]:
        return (
            self.create_file(signed_path, SIGNED_BYTES),
            self.create_file(unsigned_path, UNSIGNED_BYTES),
        )

    def run_script(
        self,
        signed_argument: str,
        unsigned_argument: str | None,
        *,
        environment: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        self.log.unlink(missing_ok=True)
        arguments = ["bash", str(VERIFY_SCRIPT), signed_argument]
        if unsigned_argument is not None:
            arguments.append(unsigned_argument)
        return subprocess.run(
            arguments,
            cwd=self.fixture_root,
            env=environment or self.environment,
            text=True,
            capture_output=True,
            check=False,
        )

    def invocation(self) -> dict[str, object]:
        return json.loads(self.log.read_text(encoding="utf-8"))

    def assert_successful_absolute_verification(
        self,
        result: subprocess.CompletedProcess[str],
        expected_signed: Path,
        expected_build_dir: Path | None = None,
    ) -> None:
        self.assertEqual(result.returncode, 0, result.stderr)
        invocation = self.invocation()
        expected = str(expected_signed.resolve(strict=True))
        self.assertEqual(invocation["datafile"], expected)
        self.assertTrue(invocation["datafile_is_absolute"])
        self.assertEqual(invocation["arguments"][-1], expected)
        expected_build = (expected_build_dir or self.build_dir).resolve()
        self.assertEqual(invocation["tool_cwd"], str(expected_build))
        self.assertTrue(Path(str(invocation["build_argument"])).is_absolute())
        self.assertIn(
            "Signed release payload matches the independent reproducible build "
            f"({len(UNSIGNED_BYTES)} authenticated bytes)",
            result.stdout,
        )
        self.assertIn(
            f"Firmware size guard: {len(SIGNED_BYTES)} / 3145728 bytes",
            result.stdout,
        )

    def assert_rejected_before_rsa(
        self, result: subprocess.CompletedProcess[str], expected_message: str
    ) -> None:
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(expected_message, result.stderr)
        self.assertFalse(self.log.exists(), "fake RSA command unexpectedly ran")

    def test_workflow_relative_paths_become_absolute(self) -> None:
        signed, unsigned = self.create_pair()
        result = self.run_script(
            "signed-release-bundle/smoker_controller.bin",
            "independent-build/smoker_controller.bin",
        )
        self.assert_successful_absolute_verification(result, signed)
        self.assertEqual(unsigned.read_bytes(), UNSIGNED_BYTES)

    def test_absolute_paths_remain_supported(self) -> None:
        signed, unsigned = self.create_pair(
            "absolute/smoker_controller.bin", "absolute/reference.bin"
        )
        result = self.run_script(str(signed.resolve()), str(unsigned.resolve()))
        self.assert_successful_absolute_verification(result, signed)

    def test_paths_with_spaces_and_shell_characters_are_single_arguments(self) -> None:
        signed, unsigned = self.create_pair(
            "signed $(not-executed);[bundle]/smoker controller.bin",
            "reference $(not-executed);[build]/expected image.bin",
        )
        result = self.run_script(
            str(signed.relative_to(self.fixture_root)),
            str(unsigned.relative_to(self.fixture_root)),
        )
        self.assert_successful_absolute_verification(result, signed)

    def test_relative_build_directory_is_canonicalized(self) -> None:
        signed, unsigned = self.create_pair()
        environment = self.environment.copy()
        environment["SMOKER_RELEASE_BUILD_DIR"] = "relative build output"
        result = self.run_script(str(signed), str(unsigned), environment=environment)
        self.assert_successful_absolute_verification(
            result, signed, self.fixture_root / "relative build output"
        )

    def test_substituted_validly_signed_payload_is_rejected(self) -> None:
        signed, unsigned = self.create_pair()
        signed.write_bytes(
            b"older-validly-signed-payload".ljust(SECTOR_SIZE, b"\xff")
            + SIGNATURE_SECTOR
        )
        result = self.run_script(str(signed), str(unsigned))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.log.exists(), "RSA fixture should accept substituted image")
        self.assertIn(
            "RSA-authenticated payload does not match the independent build",
            result.stderr,
        )

    def test_missing_or_extra_signature_sector_is_rejected(self) -> None:
        for suffix in (b"", SIGNATURE_SECTOR + SIGNATURE_SECTOR):
            with self.subTest(sectors=len(suffix) // SECTOR_SIZE):
                signed, unsigned = self.create_pair()
                signed.write_bytes(UNSIGNED_BYTES + suffix)
                result = self.run_script(str(signed), str(unsigned))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "plus exactly one ESP Secure Boot v2 signature sector",
                    result.stderr,
                )

    def test_unaligned_independent_image_is_rejected(self) -> None:
        signed, unsigned = self.create_pair()
        unsigned.write_bytes(b"not-sector-aligned")
        result = self.run_script(str(signed), str(unsigned))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is not ESP Secure Boot v2 sector-aligned", result.stderr)

    def test_missing_signed_file_is_rejected_before_rsa(self) -> None:
        unsigned = self.create_file("reference.bin", UNSIGNED_BYTES)
        result = self.run_script("missing.bin", str(unsigned))
        self.assert_rejected_before_rsa(result, "signed release image is missing")

    def test_signed_directory_is_rejected_before_rsa(self) -> None:
        directory = self.fixture_root / "not-an-image"
        directory.mkdir()
        unsigned = self.create_file("reference.bin", UNSIGNED_BYTES)
        result = self.run_script(directory.name, str(unsigned))
        self.assert_rejected_before_rsa(
            result, "signed release image is not a regular file"
        )

    def test_signed_symlink_is_rejected_before_rsa(self) -> None:
        target, unsigned = self.create_pair("target/signed.bin", "reference.bin")
        link = self.fixture_root / "linked-image.bin"
        link.symlink_to(target)
        result = self.run_script(link.name, str(unsigned))
        self.assert_rejected_before_rsa(
            result, "signed release image must not be a symlink"
        )

    def test_missing_independent_image_is_rejected_before_rsa(self) -> None:
        signed = self.create_file("signed.bin", SIGNED_BYTES)
        result = self.run_script(str(signed), "missing-reference.bin")
        self.assert_rejected_before_rsa(result, "independent unsigned image is missing")

    def test_independent_image_symlink_is_rejected_before_rsa(self) -> None:
        signed, target = self.create_pair("signed.bin", "target/reference.bin")
        link = self.fixture_root / "linked-reference.bin"
        link.symlink_to(target)
        result = self.run_script(str(signed), link.name)
        self.assert_rejected_before_rsa(
            result, "independent unsigned image must not be a symlink"
        )

    def test_independent_image_argument_is_required(self) -> None:
        signed = self.create_file("signed.bin", SIGNED_BYTES)
        result = self.run_script(str(signed), None)
        self.assert_rejected_before_rsa(
            result, "expected independent unsigned image path is required"
        )

    def test_wrong_idf_version_is_rejected_before_rsa(self) -> None:
        signed, unsigned = self.create_pair()
        environment = self.environment.copy()
        environment["FAKE_IDF_VERSION"] = "ESP-IDF v6.0.1"
        result = self.run_script(str(signed), str(unsigned), environment=environment)
        self.assert_rejected_before_rsa(
            result, "release signature verification requires exactly ESP-IDF v6.0.2"
        )

    def test_fake_idf_reproduces_old_relative_path_failure(self) -> None:
        self.create_file("signed-release-bundle/smoker_controller.bin", SIGNED_BYTES)
        result = subprocess.run(
            [
                str(self.fake_idf),
                "-B",
                str(self.build_dir),
                "secure-verify-signature",
                "--keyfile",
                "ignored-public-key.pem",
                "signed-release-bundle/smoker_controller.bin",
            ],
            cwd=self.fixture_root,
            env=self.environment,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 41)
        invocation = self.invocation()
        self.assertFalse(invocation["datafile_is_absolute"])
        self.assertEqual(invocation["tool_cwd"], str(self.build_dir.resolve()))
        self.assertFalse(
            (self.build_dir / "signed-release-bundle/smoker_controller.bin").exists()
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)

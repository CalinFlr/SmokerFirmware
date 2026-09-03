#!/usr/bin/env python3
"""Exercise signed-image path handling against ESP-IDF 6.0.2 cwd semantics."""

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
IMAGE_BYTES = b"signed-release-path-fixture\x00\x01\x02"

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

    def create_image(self, relative_path: str) -> Path:
        image = self.fixture_root / relative_path
        image.parent.mkdir(parents=True, exist_ok=True)
        image.write_bytes(IMAGE_BYTES)
        return image

    def run_script(
        self, image_argument: str, *, environment: dict[str, str] | None = None
    ) -> subprocess.CompletedProcess[str]:
        self.log.unlink(missing_ok=True)
        return subprocess.run(
            ["bash", str(VERIFY_SCRIPT), image_argument],
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
        expected_image: Path,
        expected_build_dir: Path | None = None,
    ) -> None:
        self.assertEqual(result.returncode, 0, result.stderr)
        invocation = self.invocation()
        expected = str(expected_image.resolve(strict=True))
        self.assertEqual(invocation["datafile"], expected)
        self.assertTrue(invocation["datafile_is_absolute"])
        self.assertEqual(invocation["arguments"][-1], expected)
        expected_build = (expected_build_dir or self.build_dir).resolve()
        self.assertEqual(invocation["tool_cwd"], str(expected_build))
        self.assertTrue(Path(str(invocation["build_argument"])).is_absolute())
        self.assertIn(
            f"Firmware size guard: {len(IMAGE_BYTES)} / 3145728 bytes",
            result.stdout,
        )

    def assert_rejected_before_rsa(
        self, result: subprocess.CompletedProcess[str], expected_message: str
    ) -> None:
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(expected_message, result.stderr)
        self.assertFalse(self.log.exists(), "fake RSA command unexpectedly ran")

    def test_workflow_relative_path_becomes_absolute(self) -> None:
        image = self.create_image("signed-release-bundle/smoker_controller.bin")
        result = self.run_script("signed-release-bundle/smoker_controller.bin")
        self.assert_successful_absolute_verification(result, image)

    def test_absolute_path_remains_supported(self) -> None:
        image = self.create_image("absolute/smoker_controller.bin")
        result = self.run_script(str(image.resolve()))
        self.assert_successful_absolute_verification(result, image)

    def test_path_with_spaces_and_shell_characters_is_one_argument(self) -> None:
        relative = "signed release $(not-executed);[bundle]/smoker controller.bin"
        image = self.create_image(relative)
        result = self.run_script(relative)
        self.assert_successful_absolute_verification(result, image)

    def test_relative_build_directory_is_canonicalized(self) -> None:
        image = self.create_image("relative-build/smoker_controller.bin")
        environment = self.environment.copy()
        environment["SMOKER_RELEASE_BUILD_DIR"] = "relative build output"
        result = self.run_script(str(image), environment=environment)
        self.assert_successful_absolute_verification(
            result,
            image,
            self.fixture_root / "relative build output",
        )

    def test_missing_file_is_rejected_before_rsa(self) -> None:
        result = self.run_script("missing/smoker_controller.bin")
        self.assert_rejected_before_rsa(result, "signed release image is missing")

    def test_directory_is_rejected_before_rsa(self) -> None:
        directory = self.fixture_root / "not-an-image"
        directory.mkdir()
        result = self.run_script(directory.name)
        self.assert_rejected_before_rsa(
            result, "signed release image is not a regular file"
        )

    def test_symlink_is_rejected_before_rsa(self) -> None:
        target = self.create_image("target/smoker_controller.bin")
        link = self.fixture_root / "linked-image.bin"
        link.symlink_to(target)
        result = self.run_script(link.name)
        self.assert_rejected_before_rsa(
            result, "signed release image must not be a symlink"
        )

    def test_wrong_idf_version_is_rejected_before_rsa(self) -> None:
        image = self.create_image("wrong-version/smoker_controller.bin")
        environment = self.environment.copy()
        environment["FAKE_IDF_VERSION"] = "ESP-IDF v6.0.1"
        result = self.run_script(str(image), environment=environment)
        self.assert_rejected_before_rsa(
            result, "release signature verification requires exactly ESP-IDF v6.0.2"
        )

    def test_fake_idf_reproduces_old_relative_path_failure(self) -> None:
        self.create_image("signed-release-bundle/smoker_controller.bin")
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

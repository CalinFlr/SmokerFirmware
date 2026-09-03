#!/usr/bin/env python3
"""Mutation tests proving the release-workflow guardrail fails closed."""

from __future__ import annotations

import unittest
from pathlib import Path

from check_release_workflow import check_workflow


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/release.yml"


class ReleaseWorkflowGuardrailTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = WORKFLOW.read_text(encoding="utf-8")

    def assert_mutation_rejected(self, old: str, new: str) -> None:
        self.assertIn(old, self.source)
        mutated = self.source.replace(old, new, 1)
        self.assertTrue(check_workflow(mutated), f"guardrail accepted mutation: {new}")

    def test_valid_workflow(self) -> None:
        self.assertEqual(check_workflow(self.source), [])

    def test_top_level_write_is_rejected(self) -> None:
        self.assert_mutation_rejected("permissions:\n  contents: read", "permissions:\n  contents: write")

    def test_sign_write_is_rejected(self) -> None:
        marker = "  sign:\n"
        start = self.source.index(marker)
        suffix = self.source[start:].replace("contents: read", "contents: write", 1)
        self.assertTrue(check_workflow(self.source[:start] + suffix))

    def test_secret_outside_signing_step_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "  publish:\n",
            "  publish:\n    env:\n      LEAK: ${{ secrets.SMOKER_OTA_SIGNING_KEY_B64 }}\n",
        )

    def test_environment_outside_sign_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "  verify-signed-artifact:\n",
            "  verify-signed-artifact:\n    environment: firmware-release\n",
        )

    def test_broken_dependencies_are_rejected(self) -> None:
        self.assert_mutation_rejected("    needs: validate", "    needs: publish")
        self.assert_mutation_rejected("    needs: verify-signed-artifact", "    needs: sign")

    def test_checkout_credentials_are_rejected(self) -> None:
        self.assert_mutation_rejected("persist-credentials: false", "persist-credentials: true")

    def test_mutable_action_reference_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4.6.2",
            "actions/upload-artifact@v4",
        )

    def test_release_command_outside_publish_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "      - name: Build signed source again",
            "      - name: Build signed source again\n        run: gh release create bad\n      - name: Build signed source again",
        )

    def test_fixed_ota_name_is_rejected_when_removed(self) -> None:
        mutated = self.source.replace("smoker_controller.bin", "renamed.bin")
        self.assertTrue(check_workflow(mutated))


if __name__ == "__main__":
    unittest.main(verbosity=2)

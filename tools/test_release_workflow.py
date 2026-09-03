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

    def test_extra_trigger_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "on:\n  push:\n",
            "on:\n  pull_request:\n  push:\n",
        )

    def test_duplicate_yaml_key_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "permissions:\n  contents: read\n",
            "permissions:\n  contents: read\npermissions:\n  contents: read\n",
        )

    def test_older_same_tag_run_must_be_cancelled(self) -> None:
        self.assert_mutation_rejected(
            "cancel-in-progress: true", "cancel-in-progress: false"
        )

    def test_sign_write_is_rejected(self) -> None:
        marker = "  sign:\n"
        start = self.source.index(marker)
        suffix = self.source[start:].replace("contents: read", "contents: write", 1)
        self.assertTrue(check_workflow(self.source[:start] + suffix))

    def test_additional_sign_permission_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "    permissions:\n      contents: read\n    timeout-minutes: 35",
            "    permissions:\n      contents: read\n      actions: write\n    timeout-minutes: 35",
        )

    def test_sign_dependency_bypass_condition_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "  sign:\n    name:",
            "  sign:\n    if: always()\n    name:",
        )

    def test_step_continue_on_error_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "      - name: Sign and verify with the release key\n",
            "      - name: Sign and verify with the release key\n        continue-on-error: true\n",
        )

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

    def test_unreviewed_publish_step_is_rejected(self) -> None:
        marker = "  publish:\n"
        start = self.source.index(marker)
        suffix = self.source[start:].replace(
            "    steps:\n",
            "    steps:\n"
            "      - name: Write before verification\n"
            "        run: curl https://api.github.invalid\n",
            1,
        )
        self.assertTrue(check_workflow(self.source[:start] + suffix))

    def test_live_tag_commit_resolution_is_required(self) -> None:
        self.assert_mutation_rejected(
            "commits/$GITHUB_REF_NAME",
            "commits/main",
        )

    def test_tag_update_event_is_rejected(self) -> None:
        self.assert_mutation_rejected(
            "TAG_CREATED: ${{ github.event.created }}",
            "TAG_CREATED: true",
        )

    def test_fixed_ota_name_is_rejected_when_removed(self) -> None:
        mutated = self.source.replace("smoker_controller.bin", "renamed.bin")
        self.assertTrue(check_workflow(mutated))


if __name__ == "__main__":
    unittest.main(verbosity=2)

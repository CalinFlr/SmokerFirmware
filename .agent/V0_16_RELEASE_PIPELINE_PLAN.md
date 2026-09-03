# v0.16.0 Isolated Release Pipeline Plan

## Goal

Prepare the next firmware release line at version `0.16.0` and separate GitHub
Actions validation, signing, independent verification, and publication into
least-privilege jobs, then push an unmerged pull request without creating or
moving any tag or publishing a release.

## Scope

- Bump the authoritative current version from `0.15.0` to `0.16.0`.
- Add a deterministic strict release-bundle manifest creator/verifier and host
  tests for valid and adversarial bundles.
- Split `.github/workflows/release.yml` into `validate -> sign ->
  verify-signed-artifact -> publish`.
- Add executable workflow privilege/pinning guardrails to host verification.
- Update current-release documentation and the manual release runbook.
- Run all required host, ESP-IDF, combined, helper, and diff validations.
- Push `ci/v0.16-release-pipeline` and open (but do not merge) the preparation
  pull request.

## Non-goals

- Creating, moving, deleting, or reusing any tag.
- Creating or publishing any GitHub Release.
- Modifying Blynk Console state, firmware behavior, signing keys, RSA-3072,
  OTA image naming/format/URL, partition layout, rollback, hardware, local
  credentials, or other firmware features.
- Secure Boot/eFuse, flash encryption, key rotation, SBOM, or attestation work.

## Current repository observations

- PR #11 is merged into `main` at `583176bd5b1516c735b23347447c712e82d30a39`.
- Local `main` and `origin/main` matched that commit before branch creation.
- The merged contract ignores `CmdStartRequest=0`, maps `CmdStartRequest=1`
  atomically to Start, and rejects deprecated `CmdStart`/`CmdStartTargetC`.
- `version.txt` is `0.15.0`.
- Historical annotated tag `v0.15.0` resolves to old commit
  `4fa9ea912acbe87b6e26fb1aaa9515ee6faeef77`, which is an ancestor of current
  `main` and must remain unchanged.
- The latest public GitHub Release is `v0.14.1`; there is no `v0.15.0` release.
- GitHub has no `v0.16.0` tag or release.
- The existing release workflow combines validation, environment-held signing
  secret, `contents: write`, and publication in one job.
- Current roadmap state remains M15 implemented with documented target gaps;
  this task changes release engineering only and makes no hardware claim.

## Assumptions

- `firmware-release` continues to hold only the existing
  `SMOKER_OTA_SIGNING_KEY_B64` secret and any configured approval policy.
- GitHub artifact upload/download are acceptable transient transport between
  isolated jobs when pinned to immutable official-action SHAs.
- A ZIP artifact is transport only; the bounded release bundle is the strict
  set of approved files checked by the helper before and after transport.

## Steps

1. Inventory every current-version reference and release/runbook statement;
   preserve historical version evidence.
2. Implement a standard-library Python helper that creates and verifies a
   canonical strict manifest binding schema, version, tag, commit, image name,
   SHA-256, and size; reject missing/extra files and inconsistent metadata.
3. Add no-network host fixtures covering the valid bundle, every required
   tamper/mismatch case, deterministic output, strict SemVer, and exact
   `version.txt` value.
4. Implement a source-aware workflow guardrail for permissions, secret scope,
   environment scope, job dependencies, checkout behavior, action SHA pins,
   release command placement, and fixed OTA filename; wire both executable
   helper test suites into `tools/verify.sh --host-only`.
5. Refactor the tag workflow into four jobs with exact-SHA checkout, isolated
   privileges, bounded artifact transfer, local and independent RSA
   verification, and publish-time manifest/hash revalidation without rebuild or
   signing.
6. Bump current version references to `0.16.0` and update decisions, roadmap,
   traceability, README/runbook, and key documentation only where they describe
   the current or next release.
7. Validate helper tests, workflow guardrail, host tests/sanitizers/guardrails,
   ESP-IDF v6.0.2 build, combined verification, workflow syntax when a local
   tool is available, and `git diff --check`.
8. Review the complete diff and invariant searches, commit, push, open the PR
   with the required graph/matrix/test/limitation/manual-step report, and verify
   remotely that no tag/release was created and the PR remains open/unmerged.

## Validation commands

```text
python3 tools/test_release_bundle.py
python3 tools/check_release_workflow.py
bash tools/verify.sh --host-only
bash tools/verify.sh --idf-only
bash tools/verify.sh
git diff --check
```

If installed locally, validate the workflow with `actionlint` and report its
exact version. No release workflow will be triggered as a test.

## Risks / unresolved items

- The signing-key loss and compromise risks documented by D050/D051 remain;
  job separation limits privilege co-location but is not independent key
  custody, Secure Boot, or flash encryption.
- GitHub artifact service is an untrusted transport boundary for this design;
  independent public-key verification, strict manifest checking, exact commit
  binding, and file allowlisting must detect alteration before publication.
- Target/hardware behavior is intentionally untested and unchanged by this CI
  task.

## Progress

- Steps 1-6 are complete. The implementation uses the exact four-job graph,
  a canonical strict three-file bundle, reusable public-key verification, and
  executable privilege/pinning and bundle checks.
- Step 7 is complete: helper and mutation suites pass; `tools/verify.sh
  --host-only`, `--idf-only`, and the combined invocation pass; `git diff
  --check`, `bash -n`, Python byte compilation, and Ruby Psych YAML parsing
  pass. Local `actionlint` and `shellcheck` are not installed, so neither was
  claimed or added as a dependency.
- Step 8 is complete: the staged diff and invariants were reviewed, commit
  `130916a` was pushed, PR #12 was opened with the required report, and both
  push- and pull-request-triggered host/ESP-IDF checks passed. Remote checks
  confirmed that `v0.15.0` did not move, `v0.16.0` and its release do not
  exist, and the PR remains open and unmerged.

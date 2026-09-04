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
  `SMOKER_OTA_SIGNING_KEY_B64` secret and tag deployment policy. D051 records
  that it currently has no required reviewer.
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

## Review closeout

Goal

Close the four current PR #12 review threads without widening the release
scope or creating a tag, release, or merge.

Current repository observations

- PR #12 head is `cfad0ee9e26912b4ceee68e32698484d86d66361`; current `main`
  remains its base at `583176bd5b1516c735b23347447c712e82d30a39`.
- All four threads are current, unresolved, and valid: live tag/commit binding,
  semantic workflow validation, the nonexistent environment approval step,
  and missing bundle invariant fixtures.
- The `firmware-release` environment restricts deployment to `v*.*.*` tags and
  owns the signing secret, but has no required reviewer under D051.

Steps

1. Make stale tag-push runs fail closed by cancelling an older same-tag run,
   requiring a newly created tag event, and comparing the live peeled tag
   commit with `GITHUB_SHA` immediately before publication.
2. Replace string-presence-only workflow assertions with a safe semantic YAML
   structure parse plus exact trigger, job graph, permissions, environment,
   dependency, condition, and checkout assertions; retain source checks where
   comments or command placement are intentionally relevant.
3. Add no-network workflow mutations for extra triggers, dependency bypass,
   extra permissions, duplicate YAML keys, and removal of the live tag check.
4. Correct the runbook and PR description to state the actual D051 automatic
   environment admission instead of claiming a nonexistent approval pause.
5. Add bundle fixtures for schema, size, duplicate-key, and symlink rejection.
6. Run both fixture suites, host-only, IDF-only, combined verification, and
   `git diff --check`; review the complete branch diff and invariants.
7. Commit and push the closeout, wait for current-head CI, reply to and resolve
   each thread with its evidence, request re-review, and recheck tags/releases.

Risks / unresolved items

- A publish-time live-tag comparison closes the stale-run mismatch window but
  does not make tags immutable after publication. Repository tag protection is
  operational defense in depth and remains outside this source-only closeout.
- Ruby Psych is already used to syntax-check this workflow locally. The
  executable guard will use its AST to preserve the literal `on` key, reject
  aliases and duplicate mappings, and avoid YAML 1.1 boolean-key ambiguity.

Closeout progress

- Steps 1-5 are complete. The workflow rejects tag-update events, cancels an
  older same-tag run, compares the live peeled tag commit before publication,
  and retains the original four-job privilege chain. The host guard now parses
  YAML structure through Psych and rejects the reviewed semantic bypasses.
- Step 6 is complete: bundle fixtures pass 23/23, workflow mutations pass
  19/19, `tools/verify.sh --host-only`, `--idf-only`, and the combined command
  pass, and the unchanged target image remains 1,441,792 bytes under ESP-IDF
  6.0.2. No hardware behavior was exercised or claimed.
- Step 7 remains: commit/push, current-head CI, thread replies/resolution,
  re-review request, and final remote tag/release audit.

## Independent RSA image-path closeout

Goal

Make the independent RSA helper pass an absolute canonical image path to
ESP-IDF 6.0.2 and prove the build-directory execution behavior without
triggering a release.

Current repository observations

- PR #12 head `2899a861df38526a7d59b4ff5ae6d0a6bb7666d4` and current `main`
  `583176bd5b1516c735b23347447c712e82d30a39` match the expected closeout
  state.
- `verify-signed-artifact` passes
  `signed-release-bundle/smoker_controller.bin`, while the helper forwards that
  relative value unchanged to `idf.py`.
- ESP-IDF 6.0.2 runs `espsecure` with the build directory as its working
  directory, so the precheck and RSA tool currently resolve the same argument
  from different directories. Ordinary PR CI does not execute this tag-only
  path.

Steps

1. Keep the input separate, reject missing, non-regular, and symlink images,
   then canonicalize the image with Python `Path.resolve(strict=True)` before
   invoking `idf.py` or the size guard.
2. Preserve the exact ESP-IDF version, RSA command, public key, size threshold,
   immediate signing-job verification, and independent artifact verification.
3. Add a no-network host harness with a fake `idf.py` that changes to `-B`,
   rejects relative datafiles, records its arguments, and accepts only an
   existing absolute image.
4. Cover the workflow-relative path, absolute path, spaces, missing file,
   directory, symlink, wrong ESP-IDF version, and a direct demonstration that
   the simulated ESP-IDF behavior rejects the old relative argument.
5. Integrate the harness into host verification, run the full required local
   matrix, review scope/invariants, commit, and push.
6. Record the cause/fix/test on PR #12, request re-review, wait for current-head
   CI, resolve any resulting thread only after evidence, and re-audit tags,
   releases, mergeability, and zero unresolved threads.

Validation commands

```text
python3 tools/test_verify_signed_release_firmware.py
python3 tools/test_release_bundle.py
python3 tools/test_release_workflow.py
python3 tools/check_release_workflow.py
bash tools/verify.sh --host-only
bash tools/verify.sh --idf-only
bash tools/verify.sh
git diff --check
bash -n tools/verify_signed_release_firmware.sh
```

Risks / unresolved items

- The fake IDF harness proves argument and working-directory semantics, not a
  cryptographic signature. Existing ESP-IDF target checks and the unchanged
  `secure-verify-signature` command remain the RSA mechanism evidence.
- No tag or release workflow execution is permitted for this regression test.

RSA path closeout progress

- Steps 1-4 are complete. The helper rejects missing, non-regular, and symlink
  inputs, resolves the accepted image strictly, canonicalizes the build path,
  and passes the same absolute image to RSA and size verification.
- Step 5 local validation is complete: the dedicated harness passes 9/9,
  release bundle tests pass 23/23, workflow mutations pass 19/19, host and
  sanitizer suites pass, and `--idf-only` plus the combined ESP-IDF 6.0.2
  verification retain the 1,441,792-byte target image.
- Step 6 remains: commit/push, PR evidence, current-head CI and re-review, then
  the final zero-thread/tag/release/merge audit.

## Signed payload identity closeout

Goal

Prevent an untrusted artifact handoff from substituting an older image signed
by the same OTA key while rewriting the unsigned manifest identity.

Current repository observations

- Re-review on head `a0468c70458e99ea5854c654cbdd325314fed7ce`
  identified one valid unresolved P1: RSA authenticates only the image, while
  the manifest and sidecar travel beside it and can be rewritten together.
- ESP-IDF 6.0.2 Secure Boot v2 signs the sector-aligned input bytes and appends
  exactly one 4 KiB signature sector. The ordinary verified unsigned image is
  already sector-aligned.
- Independent verification checks out the exact triggering commit without a
  secret, so it can derive the expected authenticated payload itself.

Steps

1. Enable ESP-IDF reproducible application builds and enforce the effective
   setting so signing and independent jobs derive identical bytes.
2. Rebuild the exact checkout without the signing key in
   `verify-signed-artifact`.
3. After public-key RSA verification, require the signed image to contain the
   exact independent build followed by exactly one Secure Boot v2 signature
   sector.
4. Carry the verified image digest through the workflow control plane and make
   `publish` bind its separately downloaded copy to that digest before bundle
   revalidation; do not add a rebuild to the write-privileged job or alter the
   three-file bundle.
5. Add no-network regression cases proving that a differently signed payload
   accepted by the RSA fixture still fails identity binding, plus boundary,
   path, missing, and symlink cases.
6. Extend semantic workflow mutations, source-of-truth documentation, and the
   complete local validation matrix.
7. Commit/push, report the evidence on the P1 thread, wait for current-head CI,
   resolve only with proof, request another review, and repeat the remote
   tag/release/merge audit.

Risks / unresolved items

- The fake RSA harness proves orchestration and fail-closed payload comparison,
  not cryptography; the pinned ESP-IDF 6.0.2 command remains the signature
  evidence.
- Reproducibility must be demonstrated with clean independent target build
  directories before the workflow change is accepted.

Payload identity closeout progress

- Steps 1-5 are complete. The 15-case executable harness rejects a substituted
  payload even after its fake RSA verifier accepts it, and covers exact
  signature-sector length plus both signed/reference path boundaries.
- Step 6 is complete. The workflow suite passes 24/24 mutations, and two clean
  ESP-IDF 6.0.2 build directories produced identical 1,441,792-byte images with
  SHA-256 `91d4fe0266f71625358b509eed9f9a704ad185e45033e7ab0eb6f9cc2d0e8ffe`.
- Step 7 local validation is complete: all dedicated suites, host/sanitizers,
  IDF-only, combined verification, shell syntax, and diff checks pass. The
  remaining work is commit/push, P1 response and resolution, current-head
  CI/re-review, and the remote invariant audit.

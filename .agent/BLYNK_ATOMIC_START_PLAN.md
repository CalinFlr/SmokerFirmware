# Blynk Atomic Start Plan

Status: complete locally — the Push-button `0` release, executable coverage,
mobile/web widget contract, guardrails, host/sanitizer suite, and ESP-IDF 6.0.2
cross-build pass; manual Console and connected-target validation remain owner
actions.

## Goal

Replace the two-message Blynk Start protocol with one bounded atomic
`CmdStartRequest` message, ignore its exact UI release value `0`, reject the two
legacy datastreams fail-closed, and deliver the change as a separate unmerged
pull request.

## Scope

- strict fixed-buffer parsing of exactly `1` or `1,<target_celsius>`;
- exact `0` release/reset handling as an ignored, correlation-free no-op;
- startup-recipe Start, explicit target override, and `-273.1` monitoring-only;
- explicit deprecated handling for `CmdStart` and `CmdStartTargetC`;
- service ID allocation, bounded remote-error feedback, correlation tracking,
  callback allowlisting, and reconnect-generation integration;
- focused executable M15 tests, source/documentation guardrails, and manual
  Blynk Console migration documentation grounded in real mobile/web widgets;
- host/sanitizer and ESP-IDF 6.0.2 cross-build validation.

## Non-goals

- no local HTTP/UI schema, application Start semantics, safety, heater, history,
  readiness, OTA design, provisioning, status-output, firmware-version, hardware,
  MAX31865, PID, or ADS1115 changes;
- no retained publish, get/sync/replay, JSON command envelope, or generic MQTT
  protocol framework;
- no Blynk Console mutation, merge, release, flash, or hardware validation.

## Current repository observations

- On 2026-08-31, freshly fetched `main` and `origin/main` are both
  `5049ff89e87c11fe3c0659d8c72112c7caa95117`.
- Existing PR #11 is open and clean at
  `cb359ff72019fe16bafe2cdffa336d18b4750b95` on
  `fix/blynk-atomic-start`; no base update is required.
- GitHub reports PR #8 (`d060b53`), PR #9 (`76c103d`), and PR #10
  (`5049ff8`) merged into `main`; the requested branch starts at that exact tip.
- `BlynkCommandMapper` is already stateless for Start and exposes `Ignored`,
  `Malformed`, and `Deprecated` decisions; exact `0` currently falls through to
  malformed atomic parsing and creates the false release alert.
- `BlynkService::process_inbound()` allocates a session ID for
  `CmdStartRequest`, queues a `RemoteError` only for non-empty mapped error text,
  and admits/tracks only `Accepted` mappings.
- The raw callback mailbox has a 16-entry fixed capacity, an exact ten-name
  allowlist, and a reserved final Stop slot.
- Connection generations tag both Blynk mailbox stages; disconnect cleanup,
  stale-generation discard, result/event cleanup, and round-robin Stop behavior
  already have executable coverage and must remain intact.
- The active roadmap milestone is M15. MAX31865 is active, PID/ADS1115 remain
  staged and inactive, and the production heater remains simulated.

## Assumptions

- Both legacy datastream names remain callback-allowlisted only so firmware can
  reject them explicitly; they are not active template controls.
- An ignored `CmdStartRequest=0` may consume a session ID because parsing stays
  single-sourced in the mapper. The ID is not application-visible and the task
  explicitly permits this internal gap; generic ID allocation is unchanged.
- `-273.1` remains a wire sentinel only and maps immediately to
  `std::nullopt`; it is never stored as a domain temperature.
- Existing command-result correlation begins only after a fully mapped command
  is admitted to the application mailbox. Deprecated or malformed requests
  produce remote error events and no semantic-success record.

## Steps

1. Map exact `CmdStartRequest=0` to `Ignored` before atomic Start parsing,
   without creating a command or retained state; leave the service's existing
   accepted-only admission/correlation and non-empty-error routing intact.
2. Extend executable M15 coverage for the release no-op, default and fixed-
   target Push press/release sequences, retained strict malformed matrix,
   legacy fail-closed behavior, and absence of release feedback/correlation.
3. Update architecture guardrails and the Blynk template/manual migration,
   architecture, safety, decision, roadmap/traceability, and relevant README
   wording for mobile Push ON/OFF, dynamic String input/presets, and the web
   numeric-widget limitation without rewriting historical target evidence.
4. Run focused tests, full required host and ESP-IDF 6.0.2 verification, and a
   requirement-by-requirement completion audit.
5. Commit and push `fix/blynk-atomic-start`, update existing PR #11, post a
   technical review response, confirm zero unresolved threads/green CI, request
   re-review, and leave the PR unmerged.

## Validation commands

```sh
bash tools/verify.sh --host-only
bash tools/verify.sh --idf-only
git diff --check origin/main...HEAD
```

The full `bash tools/verify.sh` may also be run if it adds evidence beyond the
two mandatory modes.

## Risks / unresolved items

- Host tests and the ESP-IDF cross-build prove software behavior and target
  compilation, not Blynk broker delivery, a manually migrated Console, target
  execution, sensor accuracy, heater behavior, or hardware safety.
- Installing new firmware before completing the documented Console migration
  intentionally fails old Start widgets closed; this is safer than compatibility
  translation but requires explicit owner action.
- Blynk Console mobile and web widget capabilities differ. A mobile Push button
  can emit fixed String ON/OFF values, while the standard Console web Switch
  and Image Button are numeric and cannot bind directly to this String control.
- No physical Blynk Console or connected device mutation is part of this PR.

## Outcome

- `CmdStartRequest=0` now returns `Ignored` without a command, error, application
  admission, or correlation. Exact `1` and `1,<target_celsius>` still create one
  `StartSessionCommand`; startup, explicit-target, monitoring-only, and default/
  fixed-target press/release forms are executable host-tested.
- The mapper retains no Start parameter and exposes no disconnect cleanup.
  `CmdStart` and `CmdStartTargetC` return `Deprecated`, create no command, and
  map to bounded remote-protocol errors in `BlynkService`.
- The callback uses separate nine-active/two-deprecated exact allowlists. The
  service allocates a session ID only for the new Start datastream, admits only
  fully parsed commands, and retains normal application correlation tracking.
- Clean session, generation discard, result/event cleanup, no replay, reserved
  Stop admission, and round-robin transport behavior remain intact.
- The frozen template still documents 16 outputs plus nine controls (25 total)
  and now gives executable mobile Push ON/OFF, dynamic String/preset, and web
  String-widget/disabled configurations plus the fail-closed migration order.
  No Console mutation was performed.

## Execution log

- 2026-08-30: fetched `origin`, confirmed clean `main` at
  `5049ff89e87c11fe3c0659d8c72112c7caa95117`, verified merged PRs #8/#9/#10,
  and created `fix/blynk-atomic-start` from that exact tip.
- 2026-08-30: `bash tools/verify.sh --host-only` passed all 12 normal and all 12
  ASan/UBSan groups, architecture/traceability/partition guardrails, the HTTP
  fixture, and unknown-group rejection.
- 2026-08-30: `bash tools/verify.sh --idf-only` passed with ESP-IDF exactly
  6.0.2, strict C++20 across 33 project sources, all guardrails, unsigned flash
  rejection, and a 1,441,792 / 3,145,728-byte image (45.8%).
- 2026-08-30: `git diff --check` passed. No flash, monitor, provisioning,
  Blynk Console, broker, release, merge, target, heater, or hardware-safety test
  was performed.
- 2026-08-31: independent review remediation added the exact `0` no-op,
  executable release and two press/release tests, real mobile/web widget
  documentation, and matching guardrails. Fresh remote verification retained
  exact base `5049ff89e87c11fe3c0659d8c72112c7caa95117`.
- 2026-08-31: `bash tools/verify.sh --host-only` passed all 12 normal and all 12
  ASan/UBSan groups plus architecture/traceability/partition/HTTP guardrails.
- 2026-08-31: `bash tools/verify.sh --idf-only` passed with ESP-IDF exactly
  6.0.2, strict C++20 across 33 project sources, unsigned-flash rejection, and
  a 1,441,792 / 3,145,728-byte image (45.8%).
- 2026-08-31: preferred combined `bash tools/verify.sh` repeated and passed the
  complete host, sanitizer, guardrail, ESP-IDF, size, and unsigned-flash suite.

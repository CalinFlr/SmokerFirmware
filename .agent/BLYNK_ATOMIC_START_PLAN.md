# Blynk Atomic Start Plan

Status: complete — host/sanitizer and ESP-IDF 6.0.2 cross-build validated;
manual Console and connected-target validation remain owner actions.

## Goal

Replace the two-message Blynk Start protocol with one bounded atomic
`CmdStartRequest` message, reject the two legacy datastreams fail-closed, and
deliver the change as a separate unmerged pull request.

## Scope

- strict fixed-buffer parsing of exactly `1` or `1,<target_celsius>`;
- startup-recipe Start, explicit target override, and `-273.1` monitoring-only;
- explicit deprecated handling for `CmdStart` and `CmdStartTargetC`;
- service ID allocation, bounded remote-error feedback, correlation tracking,
  callback allowlisting, and reconnect-generation integration;
- focused executable M15 tests, source/documentation guardrails, and manual
  Blynk Console migration documentation;
- host/sanitizer and ESP-IDF 6.0.2 cross-build validation.

## Non-goals

- no local HTTP/UI schema, application Start semantics, safety, heater, history,
  readiness, OTA design, provisioning, status-output, firmware-version, hardware,
  MAX31865, PID, or ADS1115 changes;
- no retained publish, get/sync/replay, JSON command envelope, or generic MQTT
  protocol framework;
- no Blynk Console mutation, merge, release, flash, or hardware validation.

## Current repository observations

- Freshly fetched `main` and `origin/main` are both
  `5049ff89e87c11fe3c0659d8c72112c7caa95117`.
- GitHub reports PR #8 (`d060b53`), PR #9 (`76c103d`), and PR #10
  (`5049ff8`) merged into `main`; the requested branch starts at that exact tip.
- `BlynkCommandMapper` currently retains `pending_start_target_` plus a Boolean
  and clears them through `disconnected()`.
- `BlynkService::process_inbound()` allocates a session ID only for legacy
  `CmdStart=1`; malformed commands already become bounded `RemoteError` events.
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
- A malformed `CmdStartRequest` may consume a session ID because parsing stays
  single-sourced in the mapper. Session IDs are internal identities and may
  therefore contain gaps.
- `-273.1` remains a wire sentinel only and maps immediately to
  `std::nullopt`; it is never stored as a domain temperature.
- Existing command-result correlation begins only after a fully mapped command
  is admitted to the application mailbox. Deprecated or malformed requests
  produce remote error events and no semantic-success record.

## Steps

1. Replace the mapper's retained Start state with strict atomic parsing and an
   explicit deprecated decision; split active and deprecated allowlists.
2. Update `process_inbound()` to allocate IDs for `CmdStartRequest`, route
   malformed/deprecated decisions to distinct bounded errors, and remove mapper
   disconnect cleanup without changing generation/result/event cleanup.
3. Extend executable M15 coverage for implicit, explicit, monitoring-only,
   strict malformed, legacy fail-closed, reconnect generation, correlation,
   Stop saturation, and feedback behavior.
4. Update architecture guardrails and the Blynk template/manual migration,
   architecture, safety, decision, roadmap/traceability, and relevant README
   wording without rewriting historical target evidence.
5. Run focused tests, full required host and ESP-IDF 6.0.2 verification, and a
   requirement-by-requirement completion audit.
6. Commit, push `fix/blynk-atomic-start`, and open the requested unmerged PR.

## Validation commands

```sh
bash tools/verify.sh --host-only
bash tools/verify.sh --idf-only
git diff --check
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
- No physical Blynk Console or connected device mutation is part of this PR.

## Outcome

- `CmdStartRequest` now creates one `StartSessionCommand` from exactly `1` or
  `1,<target_celsius>`; startup, explicit-target, and monitoring-only forms are
  executable host-tested.
- The mapper retains no Start parameter and exposes no disconnect cleanup.
  `CmdStart` and `CmdStartTargetC` return `Deprecated`, create no command, and
  map to bounded remote-protocol errors in `BlynkService`.
- The callback uses separate nine-active/two-deprecated exact allowlists. The
  service allocates a session ID only for the new Start datastream, admits only
  fully parsed commands, and retains normal application correlation tracking.
- Clean session, generation discard, result/event cleanup, no replay, reserved
  Stop admission, and round-robin transport behavior remain intact.
- The frozen template now documents 16 outputs plus nine controls (25 total)
  and the fail-closed manual Console migration order. No Console mutation was
  performed.

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

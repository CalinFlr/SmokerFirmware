# M6 Hardware-Gate Split Plan

## Goal

Split the combined M6 hardware-identification milestone into an available
controller-board gate (M6A) and a blocked external sensing/output/safety gate
(M6B), without claiming that either gate has already been validated.

## Scope

- define the scope, status, dependencies, and completion evidence for M6A/M6B;
- reorganize the deferred hardware checklist around the two gates;
- update active roadmap, architecture, safety, traceability, decision-log, and
  README references whose meaning changes because of the split.

## Non-goals

- no firmware, Wi-Fi, BLE, API, UI, OTA, persistence, sensor, or SSR code;
- no invented board model, GPIO assignment, flash/PSRAM size, or electrical
  characteristic;
- no claim of target-runtime, radio, storage, electrical, or thermal validation;
- no renumbering of M7-M15 and no rewriting of completed historical plans.

## Current repository observations

- M0-M5 are complete only as a simulated application/control slice;
- the controller board is available, but its exact model/module and measured
  capabilities are not recorded in the repository;
- external chamber/probe frontends, SSR interface, and independent protection
  hardware are not yet available;
- existing documentation treats all of those facts as one blocked M6 gate.

## Assumptions

- the available controller board targets the already-approved ESP32-S3 family;
- M6A may identify and validate only the available board and its integrated
  capabilities;
- M6B remains blocked until the corresponding external components/design facts
  exist;
- final OTA partition claims require confirmed flash capacity and confirmation
  that the identified module is the intended product baseline.

## Steps

1. Replace the combined M6 roadmap entry with M6A and M6B, preserving M7-M15.
2. Split `docs/HARDWARE.md` into controller-board and external-hardware
   checklists and clarify the OTA partition gate.
3. Record the scheduling/dependency decision and update affected architecture,
   safety, traceability, and README references.
4. Check for stale active-document references, validate traceability, and review
   the patch for unsupported completion or hardware claims.

## Validation commands

```text
python3 tools/check_traceability.py
python3 tools/check_architecture.py
git diff --check
rg -n '\bM6\b|M6\+|M6-M10|M6/M13' README.md docs
```

## Risks / unresolved items

- the exact controller-board model and whether it is the final product board are
  still unknown;
- final external pin allocation requires both M6A pin restrictions and M6B
  interface requirements;
- M6A target validation and all M6B electrical/thermal validation remain future
  physical work.

## Outcome

- the active roadmap now defines M6A as ready for controller-board
  identification and M6B as blocked on external hardware;
- `docs/HARDWARE.md` contains separate evidence checklists and preserves the
  final OTA partition gate;
- D034 records the dependency split without renumbering M7-M15;
- safety, architecture, traceability, and README claims consistently map target
  runtime work to M6A and external/electrical work to M6B;
- traceability and architecture guardrails, decision-ID continuity, stale
  combined-M6 reference review, and `git diff --check` pass;
- no firmware was built or executed and no physical hardware behavior was
  validated by this documentation-only change.

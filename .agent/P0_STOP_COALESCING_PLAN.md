# P0 Stop Coalescing Correctness Plan

Status: **Complete — 2026-08-16**

## Goal

Preserve FIFO command intent when multiple `StopSession` commands are separated
by other queued commands, while retaining bounded reserved Stop admission and
coalescing only truly redundant consecutive Stops.

## Scope

- change Stop coalescing so only a trailing pending Stop is coalesced;
- preserve a distinct Stop submitted after any intervening command;
- add host regressions from both IDLE and RUNNING and retain saturation checks;
- clarify the command-admission contract in source and repository documentation;
- run the complete M0-M5 verification suite.

## Non-goals

- no concurrent or cross-task command transport;
- no command-result API changes;
- no M6 hardware, GPIO, sensor, SSR, persistence, network, OTA, or UI work;
- no change to the manual Stop OFF-cycle barrier.

## Current repository observations

- `submit()` coalesces a Stop when `stop_is_pending()` finds any Stop anywhere
  in the pending ring;
- therefore `Stop -> Start -> Stop` can report the final Stop as accepted while
  discarding it across an intervening state transition;
- the existing host test covers queue saturation and adjacent duplicate Stops,
  but not interleaved Stop sequences;
- all host, sanitizer, architecture, traceability, and ESP-IDF build checks pass
  before this patch.

## Assumptions

- consecutive pending Stops are semantically redundant and may be coalesced;
- any intervening command ends the coalescing run, so a later Stop represents a
  new FIFO intent and must occupy the reserved admission slot;
- with one earlier Stop plus later regular commands, the existing 15-command
  regular admission limit leaves capacity for that later Stop.

## Steps

1. Add failing regression coverage for interleaved Stop sequences from IDLE and
   RUNNING, plus explicit adjacent-coalescing behavior.
2. Replace the any-pending-Stop predicate with a trailing-Stop predicate.
3. Update source comments, architecture/data-model contracts, the decision log,
   and traceability evidence.
4. Run host/sanitizer verification, the ESP-IDF v6.0.2 cross-build, whitespace
   checks, and a final requirement-by-requirement audit.

## Validation commands

```sh
tools/verify.sh --host-only
tools/verify.sh --idf-only
git diff --check
```

## Risks / unresolved items

- the command API remains deliberately single-owner and non-thread-safe;
- source guardrails are not a substitute for the new behavioral regressions;
- target-runtime and physical hardware behavior remain pending at M6+.

## Outcome

- `submit()` now coalesces a Stop only when the newest pending command is Stop;
- a Stop after any intervening command retains a distinct FIFO queue entry and
  the existing reserved admission slot;
- host regressions cover interleaved `Stop -> Start -> Stop` sequences from IDLE
  and RUNNING plus consecutive Stop deduplication;
- architecture, data-model, decision, README, and traceability contracts match
  the implemented behavior;
- architecture and traceability guardrails: pass (52 rules, 16 referenced host
  tests);
- native host CTest: 5/5 pass;
- ASan/UBSan host CTest: 5/5 pass;
- ESP-IDF v6.0.2 ESP32-S3 cross-build and strict target C++20 check: pass;
- no target board or electrical behavior was exercised.

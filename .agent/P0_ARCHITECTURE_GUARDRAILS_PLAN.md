# P0 Architecture Guardrails Plan

Status: **Complete — 2026-08-16**

## Goal

Close the P0 architecture-review findings for the completed M0-M5 simulated
slice: make manual Stop an observable OFF barrier and turn the highest-value
architecture and requirements constraints into executable checks.

## Scope

- fix and test `StopSession` followed by `StartSession` in the same queued batch;
- strengthen host safety tests for OFF-state and temperature-boundary behavior;
- add dependency/source architecture checks with no external packages;
- add complete rule-to-traceability consistency checks;
- provide one local verification entrypoint and CI jobs for host and ESP-IDF;
- update only documentation affected by these contracts.

## Non-goals

- no M6 hardware, GPIO, sensor, SSR, persistence, network, OTA, or UI work;
- no target-runtime or electrical-safety claims;
- no P1 allocation/property/coverage-runtime work;
- no command-queue concurrency or future external producer transport.

## Current repository observations

- M0-M5 is complete at host-test and ESP-IDF cross-build level;
- the command queue drains every command in one tick, so queued `StopSession`
  followed by `StartSession` can finish that tick RUNNING without an OFF write;
- Stop admission under regular-command saturation is already implemented;
- architecture constraints and traceability are currently review conventions,
  not executable checks;
- traceability has no explicit rows for OTA-002 through OTA-005 and several
  implemented rows use non-specific test evidence;
- there is no CI workflow.

## Assumptions

- an accepted manual Stop is a control-cycle barrier: the Stop cycle writes OFF;
- later queued commands remain FIFO and are processed on the next tick;
- the current single-owner `submit()` contract remains unchanged;
- Python 3, CMake, Ninja, and the pinned ESP-IDF toolchain are acceptable build
  prerequisites; guardrail scripts themselves use only the Python standard
  library.

## Steps

1. Add a regression test that demonstrates the Stop/Start same-batch hazard.
2. Stop draining the command queue after an accepted manual Stop, preserving
   later commands for the next cycle and guaranteeing the final OFF write.
3. Add focused safety invariants for all non-heating states, missing target,
   maximum boundary/exceedance, latched recovery, and saturated Stop admission.
4. Add `tools/check_architecture.py` for layer imports, task ownership/count,
   heater-write ownership, submit call sites, component dependencies, and V0
   state/future-feature absence.
5. Add `tools/check_traceability.py` for exact source-rule coverage, unique
   matrix rows, valid validation markers, and real referenced host tests.
6. Update traceability and architecture/decision documentation for the Stop
   barrier and executable guardrails.
7. Add `tools/verify.sh`, host/target CI jobs, and concise README usage.
8. Run guardrails, host tests, ASan/UBSan, ESP-IDF v6.0.2 build, and diff checks.

## Validation commands

```sh
python3 tools/check_architecture.py
python3 tools/check_traceability.py
tools/verify.sh --host-only

export IDF_TOOLS_PATH="$PWD/.tools/espressif"
. "$PWD/.tools/esp-idf-v6.0.2/export.sh"
tools/verify.sh --idf-only

git diff --check
```

## Risks / unresolved items

- source checks are intentionally narrow structural guardrails, not a C++ AST
  proof or replacement for runtime safety tests;
- task watchdog/reset, stack behavior, real sensing/output, and independent
  electrical protection remain target/HW pending at M6+;
- GitHub-hosted CI configuration cannot prove behavior on a physical board.

## Outcome

- a valid manual Stop now ends command draining for its tick; a queued Start is
  preserved for the next tick and the Stop tick writes heater OFF;
- focused P0 host tests cover the Stop/Start batch, exact OFF write, all
  non-heating states, missing target, configured maximum boundary, latched
  recovery, fault clear, and the full required probe snapshot shape;
- architecture and traceability checks pass, with all 52 approved rule IDs
  represented by exactly one explicit traceability row;
- `tools/verify.sh --host-only`: guardrails pass, native CTest 5/5 pass, and
  ASan/UBSan CTest 5/5 pass;
- `tools/verify.sh --idf-only`: ESP-IDF v6.0.2 ESP32-S3 cross-build passes;
  firmware size is `0x28260`, with 84% of the smallest app partition free;
- CI now runs the host/sanitizer checks and the pinned ESP-IDF cross-build as
  separate jobs;
- no target board or electrical behavior was exercised; all M6+ target/HW gates
  remain pending.
